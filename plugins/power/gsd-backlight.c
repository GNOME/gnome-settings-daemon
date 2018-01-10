/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"
#include <stdlib.h>

#include "gsd-backlight.h"
#include "gpm-common.h"
#include "gsd-power-constants.h"
#include "gsd-power-manager.h"

#define MOCK_BRIGHTNESS_FILE "GSD_MOCK_brightness"

#ifdef HAVE_GUDEV
#include <gudev/gudev.h>
#endif /* HAVE_GUDEV */

typedef struct
{
        gboolean available;

        gint brightness_min;
        gint brightness_max;
        gint brightness_val;
        gint brightness_target;
        gint brightness_step;

#ifdef HAVE_GUDEV
        GUdevClient *udev;
        GUdevDevice *udev_device;

        GTask *active_task;
        GSList *tasks;

        gint idle_update;
#endif

        GnomeRRScreen *rr_screen;
} GsdBacklightPrivate;

enum {
        PROP_0,
        PROP_RR_SCREEN,
        PROP_AVAILABLE,
        PROP_BRIGHTNESS,
        PROP_LAST,
};

static GParamSpec *props[PROP_LAST];

G_DEFINE_TYPE_WITH_PRIVATE (GsdBacklight, gsd_backlight, G_TYPE_OBJECT)

#define GSD_BACKLIGHT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_BACKLIGHT, GsdBacklightPrivate))


#ifdef HAVE_GUDEV
static GUdevDevice*
gsd_backlight_udev_get_type (GList *devices, const gchar *type)
{
        const gchar *type_tmp;
        GList *d;

        for (d = devices; d != NULL; d = d->next) {
                type_tmp = g_udev_device_get_sysfs_attr (d->data, "type");
                if (g_strcmp0 (type_tmp, type) == 0)
                        return G_UDEV_DEVICE (g_object_ref (d->data));
        }
        return NULL;
}

/*
 * Search for a raw backlight interface, raw backlight interfaces registered
 * by the drm driver will have the drm-connector as their parent, check the
 * drm-connector's enabled sysfs attribute so that we pick the right LCD-panel
 * connector on laptops with hybrid-gfx. Fall back to just picking the first
 * raw backlight interface if no enabled interface is found.
 */
static GUdevDevice*
gsd_backlight_udev_get_raw (GList *devices)
{
        GUdevDevice *parent;
        const gchar *attr;
        GList *d;

        for (d = devices; d != NULL; d = d->next) {
                attr = g_udev_device_get_sysfs_attr (d->data, "type");
                if (g_strcmp0 (attr, "raw") != 0)
                        continue;

                parent = g_udev_device_get_parent (d->data);
                if (!parent)
                        continue;

                attr = g_udev_device_get_sysfs_attr (parent, "enabled");
                if (!attr || g_strcmp0 (attr, "enabled") != 0)
                        continue;

                return G_UDEV_DEVICE (g_object_ref (d->data));
        }

        return gsd_backlight_udev_get_type (devices, "raw");
}

static void
gsd_backlight_udev_resolve (GsdBacklight *backlight)
{
        GsdBacklightPrivate *priv = GSD_BACKLIGHT_GET_PRIVATE (backlight);
        GList *devices;

        g_assert (priv->udev != NULL);

        devices = g_udev_client_query_by_subsystem (priv->udev, "backlight");
        if (devices == NULL)
                goto out;

        /* Search the backlight devices and prefer the types:
         * firmware -> platform -> raw */
        priv->udev_device = gsd_backlight_udev_get_type (devices, "firmware");
        if (priv->udev_device != NULL)
                goto out;

        priv->udev_device = gsd_backlight_udev_get_type (devices, "platform");
        if (priv->udev_device != NULL)
                goto out;

        priv->udev_device = gsd_backlight_udev_get_raw (devices);
        if (priv->udev_device != NULL)
                goto out;

out:
        g_list_free_full (devices, g_object_unref);
}

static gboolean
gsd_backlight_udev_idle_update_cb (GsdBacklight *backlight)
{
        GsdBacklightPrivate *priv = GSD_BACKLIGHT_GET_PRIVATE (backlight);
        g_autoptr(GError) error = NULL;
        gint brightness;
        g_autofree gchar *path = NULL;
        g_autofree gchar *contents = NULL;
        priv->idle_update = 0;

        /* If we are active again now, just stop. */
        if (priv->active_task)
                return FALSE;

        path = g_build_filename (g_udev_device_get_sysfs_path (priv->udev_device), "brightness", NULL);
        if (!g_file_get_contents (path, &contents, NULL, &error)) {
                g_warning ("Could not get brightness from sysfs: %s", error->message);
                return FALSE;
        }
        brightness = atoi(contents);

        /* Only notify if brightness has changed. */
        if (brightness == priv->brightness_val)
                return FALSE;

        priv->brightness_val = brightness;
        priv->brightness_target = brightness;
        g_object_notify_by_pspec (G_OBJECT (backlight), props[PROP_BRIGHTNESS]);

        return FALSE;
}

static void
gsd_backlight_udev_idle_update (GsdBacklight *backlight)
{
        GsdBacklightPrivate *priv = GSD_BACKLIGHT_GET_PRIVATE (backlight);

        if (priv->idle_update)
                return;

        priv->idle_update = g_idle_add ((GSourceFunc) gsd_backlight_udev_idle_update_cb, backlight);
}


static void
gsd_backlight_udev_uevent (GUdevClient *client, gchar *action, GUdevDevice *device, gpointer user_data)
{
        GsdBacklight *backlight = GSD_BACKLIGHT (user_data);
        GsdBacklightPrivate *priv = GSD_BACKLIGHT_GET_PRIVATE (backlight);

        if (device != priv->udev_device)
                return;

        if (priv->tasks)
                return;

        gsd_backlight_udev_idle_update (backlight);
}


static gboolean
gsd_backlight_udev_init (GsdBacklight *backlight)
{
        GsdBacklightPrivate *priv = GSD_BACKLIGHT_GET_PRIVATE (backlight);
        const gchar* const subsystems[] = {"backlight", NULL};

        priv->udev = g_udev_client_new (subsystems);
        gsd_backlight_udev_resolve (backlight);
        if (priv->udev_device == NULL)
                return FALSE;

        priv->available = TRUE;
        priv->brightness_min = 1;
        priv->brightness_max = g_udev_device_get_sysfs_attr_as_int (priv->udev_device, "max_brightness");
        /* If the interface has less than 100 possible values, and it is of type
         * raw, then assume that 0 does not turn off the backlight completely.
         */
        if (priv->brightness_max < 99 &&
            g_strcmp0 (g_udev_device_get_sysfs_attr (priv->udev_device, "type"), "raw") == 0)
                priv->brightness_min = 0;

        priv->brightness_val = g_udev_device_get_sysfs_attr_as_int (priv->udev_device, "brightness");
        g_debug ("Using udev device with brightness from %i to %i. Current brightness is %i.", priv->brightness_min, priv->brightness_max, priv->brightness_val);

        g_signal_connect_object (priv->udev, "uevent", G_CALLBACK (gsd_backlight_udev_uevent), backlight, 0);

        return TRUE;
}

/* All the backlight helper stuff is only needed with udev. With udev we cannot
 * write the value otherwise. */


typedef struct {
        int value;
        char *value_str;
} BacklightHelperData;


static void gsd_backlight_process_taskqueue (GsdBacklight *backlight);

static void
backlight_task_data_destroy (gpointer data)
{
        BacklightHelperData *task_data = (BacklightHelperData*) data;

        g_free (task_data->value_str);
        g_free (task_data);
}

static void
gsd_backlight_set_helper_return (GsdBacklight *backlight, GTask *task, gint result, GError *error)
{
        GsdBacklightPrivate *priv = GSD_BACKLIGHT_GET_PRIVATE (backlight);
        GSList *done_tasks, *done_task;
        gint percent = ABS_TO_PERCENTAGE (priv->brightness_min, priv->brightness_max, result);

        if (error)
                g_warning ("Error executing backlight helper: %s", error->message);

        /* Remove the task that are done from the start of the list. */
        done_tasks = priv->tasks;
        done_task = g_slist_find (priv->tasks, task);
        priv->tasks = done_task->next;
        done_task->next = NULL;

        /*
         * If setting was successfull and there are no other tasks left, then
         * assume the new value is actually the current value and fire a
         * notification.
         * This happens before returning the task values.
         */
        if (error == NULL && priv->tasks == NULL) {
                g_assert (priv->brightness_target == result);

                priv->brightness_val = priv->brightness_target;
                g_debug ("New brightness value is in effect %i (%i..%i)",
                         priv->brightness_val, priv->brightness_min, priv->brightness_max);
                g_object_notify_by_pspec (G_OBJECT (backlight), props[PROP_BRIGHTNESS]);
        }

        /* The udev handler won't read while a write is pending, so queue an
         * update in case we have missed some events. */
        if (priv->tasks == NULL) {
                gsd_backlight_udev_idle_update (backlight);
        }

        /* Return all the pending tasks up and including the one we actually
         * processed. */
        while (done_tasks) {
                GTask *item;
                item = G_TASK (done_tasks->data);

                if (error)
                        g_task_return_error (item, g_error_copy (error));
                else
                        g_task_return_int (item, percent);

                done_tasks = g_slist_delete_link (done_tasks, done_tasks);
        }
}

static void
gsd_backlight_set_helper_finish (GObject *obj, GAsyncResult *res, gpointer user_data)
{
        g_autoptr(GSubprocess) proc = G_SUBPROCESS (obj);
        GTask *task = G_TASK (user_data);
        BacklightHelperData *data = g_task_get_task_data (task);
        GsdBacklight *backlight = g_task_get_source_object (task);
        GsdBacklightPrivate *priv = GSD_BACKLIGHT_GET_PRIVATE (backlight);
        g_autoptr(GError) error = NULL;

        g_assert (task == priv->active_task);
        priv->active_task = NULL;

        g_subprocess_wait_finish (proc, res, &error);

        if (error)
                goto done;

        g_spawn_check_exit_status (g_subprocess_get_exit_status (proc), &error);
        if (error)
                goto done;

done:
        gsd_backlight_set_helper_return (backlight, task, data->value, error);
        /* Start processing any tasks that were added in the meantime. */
        gsd_backlight_process_taskqueue (backlight);
}

static void
gsd_backlight_run_set_helper (GsdBacklight *backlight, GTask *task)
{
        GsdBacklightPrivate *priv = GSD_BACKLIGHT_GET_PRIVATE (backlight);
        GSubprocess *proc = NULL;
        BacklightHelperData *data = g_task_get_task_data (task);
        GError *error = NULL;

        g_assert (priv->active_task == NULL);
        priv->active_task = task;

        if (data->value_str == NULL)
                data->value_str = g_strdup_printf("%d", data->value);

        proc = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_SILENCE,
                                 &error,
                                 "pkexec", 
                                 LIBEXECDIR "/gsd-backlight-helper",
                                 g_udev_device_get_sysfs_path (priv->udev_device),
                                 data->value_str, NULL);

        if (proc == NULL) {
                gsd_backlight_set_helper_return (backlight, task, -1, error);
                return;
        }

        g_subprocess_wait_async (proc, g_task_get_cancellable (task),
                                 gsd_backlight_set_helper_finish,
                                 task);
}

static void
gsd_backlight_process_taskqueue (GsdBacklight *backlight)
{
        GsdBacklightPrivate *priv = GSD_BACKLIGHT_GET_PRIVATE (backlight);
        GSList *last;
        GTask *to_run;

        /* There is already a task active, nothing to do. */
        if (priv->active_task)
                return;

        /* Get the last added task, thereby compressing the updates into one. */
        last = g_slist_last (priv->tasks);
        if (last == NULL)
                return;

        to_run = G_TASK (last->data);

        /* And run it! */
        gsd_backlight_run_set_helper (backlight, to_run);
}
#endif /* HAVE_GUDEV */

static GnomeRROutput*
gsd_backlight_rr_find_output (GsdBacklight *backlight, gboolean controllable)
{
        GsdBacklightPrivate *priv = GSD_BACKLIGHT_GET_PRIVATE (backlight);
        GnomeRROutput *output = NULL;
        GnomeRROutput **outputs;
        guint i;

        /* search all X11 outputs for the device id */
        outputs = gnome_rr_screen_list_outputs (priv->rr_screen);
        if (outputs == NULL)
                goto out;

        for (i = 0; outputs[i] != NULL; i++) {
                gboolean builtin = gnome_rr_output_is_builtin_display (outputs[i]);
                gint backlight = gnome_rr_output_get_backlight (outputs[i]);

                g_debug("Output %d: %s, backlight %d", i, builtin ? "builtin" : "external", backlight);
                if (builtin && (!controllable || backlight >= 0)) {
                        output = outputs[i];
                        break;
                }
        }
out:
        return output;

}

gboolean
gsd_backlight_get_available (GsdBacklight *backlight)
{
        GsdBacklightPrivate *priv = GSD_BACKLIGHT_GET_PRIVATE (backlight);

        return priv->available;
}

/**
 * gsd_backlight_get_brightness
 * @backlight: a #GsdBacklight
 * @target: Output parameter for the value the target value of pending set operations.
 *
 * The backlight value returns the last known stable value. This value will
 * only update once all pending operations to set a new value have finished.
 *
 * As such, this function may return a different value from the return value
 * of the async brightness setter. This happens when another set operation was
 * queued after it was already running.
 *
 * Returns: The last stable backlight value.
 **/
gint
gsd_backlight_get_brightness (GsdBacklight *backlight, gint *target)
{
        GsdBacklightPrivate *priv = GSD_BACKLIGHT_GET_PRIVATE (backlight);

        if (!priv->available)
                return -1;

        if (target)
                *target = ABS_TO_PERCENTAGE (priv->brightness_min, priv->brightness_max, priv->brightness_target);

        return ABS_TO_PERCENTAGE (priv->brightness_min, priv->brightness_max, priv->brightness_val);
}

static void
gsd_backlight_set_brightness_val_async (GsdBacklight *backlight,
                                        int value,
                                        GCancellable *cancellable,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data)
{
        GsdBacklightPrivate *priv = GSD_BACKLIGHT_GET_PRIVATE (backlight);
        GError *error = NULL;
        GTask *task = NULL;
        GnomeRROutput *output;

        value = MIN(priv->brightness_max, value);
        value = MAX(priv->brightness_min, value);

        priv->brightness_target = value;

        task = g_task_new (backlight, cancellable, callback, user_data);
        if (!priv->available) {
                g_task_return_new_error (task, GSD_POWER_MANAGER_ERROR, GSD_POWER_MANAGER_ERROR_FAILED, "Cannot set brightness as no backlight was detected!");
                return;
        }

        if (is_mocked ()) {
                g_autofree gchar *contents = NULL;
                g_debug ("Setting mock brightness: %d", value);

                contents = g_strdup_printf ("%d", value);
                if (!g_file_set_contents (MOCK_BRIGHTNESS_FILE, contents, -1, &error)) {
                        g_warning ("Setting mock brightness failed: %s", error->message);
                        g_task_return_error (task, error);
                }
                priv->brightness_val = priv->brightness_target;
                g_object_notify_by_pspec (G_OBJECT (backlight), props[PROP_BRIGHTNESS]);
                g_task_return_int (task, gsd_backlight_get_brightness (backlight, NULL));

                return;
        }

#ifdef HAVE_GUDEV
        if (priv->udev_device != NULL) {
                BacklightHelperData *task_data;

                task_data = g_new0 (BacklightHelperData, 1);
                task_data->value = priv->brightness_target;
                g_task_set_task_data (task, task_data, backlight_task_data_destroy);

                /* Task is set up now. Queue it and ensure we are working something. */
                priv->tasks = g_slist_append (priv->tasks, task);
                gsd_backlight_process_taskqueue (backlight);

                return;
        }
#endif /* HAVE_GUDEV */

        /* Fallback to setting via GNOME RR/X11 */
        output = gsd_backlight_rr_find_output (backlight, TRUE);
        if (output) {
                if (!gnome_rr_output_set_backlight (output, value, &error)) {
                        g_warning ("Setting brightness failed: %s", error->message);
                        g_task_return_error (task, error);
                        return;
                }
                priv->brightness_val = gnome_rr_output_get_backlight (output);
                g_object_notify_by_pspec (G_OBJECT (backlight), props[PROP_BRIGHTNESS]);
                g_task_return_int (task, gsd_backlight_get_brightness (backlight, NULL));

                return;
        }

        g_task_return_new_error (task, GSD_POWER_MANAGER_ERROR, GSD_POWER_MANAGER_ERROR_FAILED, "No method to set brightness, something changed since detection!");

        /* If this happens we detected a backlight device and it is gone now. */
        g_assert_not_reached ();
}

void
gsd_backlight_set_brightness_async (GsdBacklight *backlight,
                                    gint percent,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
        GsdBacklightPrivate *priv = GSD_BACKLIGHT_GET_PRIVATE (backlight);

        gsd_backlight_set_brightness_val_async (backlight,
                                                PERCENTAGE_TO_ABS (priv->brightness_min, priv->brightness_max, percent),
                                                cancellable,
                                                callback,
                                                user_data);
}

/**
 * gsd_backlight_set_brightness_finish
 * @backlight: a #GsdBacklight
 * @res: the #GAsyncResult passed to the callback
 * @error: #GError return address
 *
 * Finish an operation started by gsd_backlight_set_brightness_async,
 * gsd_backlight_step_up_async or gsd_backlight_step_down_async. Will return
 * the value that was actually set (which may be different because of rounding
 * or as multiple set actions where queued up).
 *
 * Please note that a call to gsd_backlight_get_brightness may not in fact
 * return the same value if further operations to set the value are pending.
 *
 * Returns: The brightness that was set.
 **/
gint
gsd_backlight_set_brightness_finish (GsdBacklight *backlight,
                                    GAsyncResult *res,
                                    GError **error)
{
        return g_task_propagate_int (G_TASK (res), error);
}

void
gsd_backlight_step_up_async (GsdBacklight *backlight,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
        GsdBacklightPrivate *priv = GSD_BACKLIGHT_GET_PRIVATE (backlight);
        gint value;

        value = priv->brightness_target + priv->brightness_step;

        gsd_backlight_set_brightness_val_async (backlight,
                                                value,
                                                cancellable,
                                                callback,
                                                user_data);
}

void
gsd_backlight_step_down_async (GsdBacklight *backlight,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
        GsdBacklightPrivate *priv = GSD_BACKLIGHT_GET_PRIVATE (backlight);
        gint value;

        value = priv->brightness_target - priv->brightness_step;

        gsd_backlight_set_brightness_val_async (backlight,
                                                value,
                                                cancellable,
                                                callback,
                                                user_data);
}


gint
gsd_backlight_get_output_id (GsdBacklight *backlight)
{
        GnomeRROutput *output;

        output = gsd_backlight_rr_find_output (backlight, FALSE);
        if (output == NULL)
                return -1;

        /* XXX: Is this really that simple? The old code did a lot more, but
         * did not return anything sensible these days.
         * The outputs need to be in the same order as the MetaScreen object
         * returns to the shell. */
        return gnome_rr_output_get_id (output);
}

static void
gsd_backlight_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
        GsdBacklight *backlight = GSD_BACKLIGHT (object);
        GsdBacklightPrivate *priv = GSD_BACKLIGHT_GET_PRIVATE (backlight);

        switch (prop_id) {
        case PROP_RR_SCREEN:
                g_value_set_object (value, priv->rr_screen);
                break;

        case PROP_AVAILABLE:
                g_value_set_boolean (value, priv->available);
                break;

        case PROP_BRIGHTNESS:
                g_value_set_int (value, gsd_backlight_get_brightness (backlight, NULL));
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsd_backlight_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
        GsdBacklightPrivate *priv = GSD_BACKLIGHT_GET_PRIVATE (object);

        switch (prop_id) {
        case PROP_RR_SCREEN:
                priv->rr_screen = g_value_dup_object (value);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsd_backlight_constructed (GObject *object)
{
        GsdBacklight *backlight = GSD_BACKLIGHT (object);
        GsdBacklightPrivate *priv = GSD_BACKLIGHT_GET_PRIVATE (object);
        GnomeRROutput* output = NULL;

        /* If mocked, set as available and set the brightness (which will also
         * create the file for the test environment). */
        if (is_mocked ()) {
                g_debug ("Using mock for backlight.");
                priv->available = TRUE;
                priv->brightness_min = 0;
                priv->brightness_max = 100;

                gsd_backlight_set_brightness_async (backlight, GSD_MOCK_DEFAULT_BRIGHTNESS, NULL, NULL, NULL);

                goto done;
        }

#ifdef HAVE_GUDEV
        /* Try finding a udev device. */
        if (gsd_backlight_udev_init (backlight))
                goto done;
#endif /* HAVE_GUDEV */

        /* Try GNOME RR as a fallback. */
        output = gsd_backlight_rr_find_output (backlight, TRUE);
        if (output) {
                g_debug ("Using GNOME RR (mutter) for backlight.");
                priv->available = TRUE;
                priv->brightness_min = 0;
                priv->brightness_max = 100;
                priv->brightness_val = gnome_rr_output_get_backlight (output);
                priv->brightness_step = gnome_rr_output_get_min_backlight_step (output);
        }

        g_debug ("No usable backlight found.");

done:
        priv->brightness_target = priv->brightness_val;
        priv->brightness_step = MAX(priv->brightness_step, BRIGHTNESS_STEP_AMOUNT(priv->brightness_max - priv->brightness_min + 1));
}

static void
gsd_backlight_finalize (GObject *object)
{
        GsdBacklightPrivate *priv = GSD_BACKLIGHT_GET_PRIVATE (object);

#ifdef HAVE_GUDEV
        g_assert (priv->active_task == NULL);
        g_assert (priv->tasks == NULL);
        g_clear_object (&priv->udev);
        g_clear_object (&priv->udev_device);
#endif /* HAVE_GUDEV */

        g_clear_object (&priv->rr_screen);
}

static void
gsd_backlight_class_init (GsdBacklightClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->constructed = gsd_backlight_constructed;
        object_class->finalize = gsd_backlight_finalize;
        object_class->get_property = gsd_backlight_get_property;
        object_class->set_property = gsd_backlight_set_property;

        props[PROP_RR_SCREEN] = g_param_spec_object ("rr-screen", "GnomeRRScreen",
                                                     "GnomeRRScreen usable for backlight control.",
                                                     GNOME_TYPE_RR_SCREEN,
                                                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

        props[PROP_AVAILABLE] = g_param_spec_boolean ("available", "Brightness controll availability",
                                                      "Whether screen brightness for the internal screen can be controlled.",
                                                      FALSE,
                                                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

        props[PROP_BRIGHTNESS] = g_param_spec_int ("brightness", "The display brightness",
                                                   "The brightness of the internal display in percent.",
                                                   0, 100, 100,
                                                   G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

        g_object_class_install_properties (object_class, PROP_LAST, props);
}


static void
gsd_backlight_init (GsdBacklight *backlight)
{
        GsdBacklightPrivate *priv = GSD_BACKLIGHT_GET_PRIVATE (backlight);

        priv->available = FALSE;
        priv->brightness_target = -1;
        priv->brightness_min = -1;
        priv->brightness_max = -1;
        priv->brightness_val = -1;
        priv->brightness_step = 1;

#ifdef HAVE_GUDEV
        priv->active_task = NULL;
        priv->tasks = NULL;
#endif /* HAVE_GUDEV */
}

GsdBacklight *
gsd_backlight_new (GnomeRRScreen *rr_screen)
{
        return g_object_new (GSD_TYPE_BACKLIGHT, "rr-screen", rr_screen, NULL);
}

