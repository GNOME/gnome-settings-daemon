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
#include <stdint.h>

#include "gnome-settings-bus.h"
#include "gsd-backlight.h"
#include "gpm-common.h"
#include "gsd-power-constants.h"
#include "gsd-power-manager.h"

struct _GsdBacklight
{
        GObject object;

        gint brightness_min;
        gint brightness_max;
        gint brightness_val;
        gint brightness_target;
        gint brightness_step;

        uint32_t backlight_serial;
        char *backlight_connector;

        gboolean initialized;

        gboolean builtin_display_disabled;
};

enum {
        PROP_BRIGHTNESS = 1,
        PROP_LAST,
};

#define MUTTER_DBUS_NAME                       "org.gnome.Mutter.DisplayConfig"
#define MUTTER_DBUS_PATH                       "/org/gnome/Mutter/DisplayConfig"
#define MUTTER_DBUS_INTERFACE                  "org.gnome.Mutter.DisplayConfig"

static GParamSpec *props[PROP_LAST];

static void     gsd_backlight_initable_iface_init (GInitableIface  *iface);
static gboolean gsd_backlight_initable_init       (GInitable       *initable,
                                                   GCancellable    *cancellable,
                                                   GError         **error);

G_DEFINE_TYPE_EXTENDED (GsdBacklight, gsd_backlight, G_TYPE_OBJECT, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                               gsd_backlight_initable_iface_init))

/**
 * gsd_backlight_get_brightness
 * @backlight: a #GsdBacklight
 *
 * The backlight value returns the last known stable value. This value will
 * only update once all pending operations to set a new value have finished.
 *
 * As such, this function may return a different value from the return value
 * of the async brightness setter. This happens when another set operation was
 * queued after it was already running.
 *
 * If the internal display is detected as disabled, then the function will
 * instead return -1.
 *
 * Returns: The last stable backlight value or -1 if the internal display is disabled.
 **/
gint
gsd_backlight_get_brightness (GsdBacklight *backlight)
{
        if (backlight->builtin_display_disabled)
                return -1;

        return ABS_TO_PERCENTAGE (backlight->brightness_min,
                                  backlight->brightness_max,
                                  backlight->brightness_val);
}

/**
 * gsd_backlight_get_target_brightness
 * @backlight: a #GsdBacklight
 *
 * This returns the target value of the pending operations, which might differ
 * from the last known stable value, but is identical to the last value set in
 * the async setter.
 *
 * If the internal display is detected as disabled, then the function will
 * instead return -1.
 *
 * Returns: The current target backlight value or -1 if the internal display is disabled.
 **/
gint
gsd_backlight_get_target_brightness (GsdBacklight *backlight)
{
        if (backlight->builtin_display_disabled)
                return -1;

        return ABS_TO_PERCENTAGE (backlight->brightness_min,
                                  backlight->brightness_max,
                                  backlight->brightness_target);
}

static void update_mutter_backlight (GsdBacklight *backlight);

static void
gsd_backlight_set_brightness_val_async (GsdBacklight *backlight,
                                        int value,
                                        GCancellable *cancellable,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data)
{
        GError *error = NULL;
        g_autoptr(GTask) task = NULL;
        const char *monitor;
        GsdDisplayConfig *display_config =
                gnome_settings_bus_get_display_config_proxy ();

        value = MIN(backlight->brightness_max, value);
        value = MAX(backlight->brightness_min, value);

        backlight->brightness_target = value;

        task = g_task_new (backlight, cancellable, callback, user_data);
        if (!backlight->initialized) {
                g_task_return_new_error (task, GSD_POWER_MANAGER_ERROR,
                                         GSD_POWER_MANAGER_ERROR_FAILED,
                                         "No backend initialized yet");
                return;
        }

        monitor = backlight->backlight_connector;
        if (!monitor) {
                g_assert_not_reached ();

                g_task_return_new_error (task, GSD_POWER_MANAGER_ERROR,
                                         GSD_POWER_MANAGER_ERROR_FAILED,
                                         "No method to set brightness!");
                return;
        }

        while (TRUE) {
                uint32_t serial = backlight->backlight_serial;

                g_clear_error (&error);

                if (gsd_display_config_call_set_backlight_sync (display_config,
                                                                serial,
                                                                monitor,
                                                                value,
                                                                NULL,
                                                                &error))
                        break;

                update_mutter_backlight (backlight);
                if (backlight->backlight_serial == serial) {
                        g_task_return_error (task, error);
                        return;
                }
        }

        backlight->brightness_val = value;
        g_object_notify_by_pspec (G_OBJECT (backlight), props[PROP_BRIGHTNESS]);
        g_task_return_int (task, gsd_backlight_get_brightness (backlight));

}

void
gsd_backlight_set_brightness_async (GsdBacklight *backlight,
                                    gint percent,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
        /* Overflow/underflow is handled by gsd_backlight_set_brightness_val_async. */
        gsd_backlight_set_brightness_val_async (backlight,
                                                PERCENTAGE_TO_ABS (backlight->brightness_min,
                                                                   backlight->brightness_max,
                                                                   percent),
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
 * Finish an operation started by gsd_backlight_set_brightness_async(). Will
 * return the value that was actually set (which may be different because of
 * rounding or as multiple set actions were queued up).
 *
 * Please note that a call to gsd_backlight_get_brightness() may not in fact
 * return the same value if further operations to set the value are pending.
 *
 * Returns: The brightness in percent that was set.
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
        gint value;

        /* Overflows are handled by gsd_backlight_set_brightness_val_async. */
        value = backlight->brightness_target + backlight->brightness_step;

        gsd_backlight_set_brightness_val_async (backlight,
                                                value,
                                                cancellable,
                                                callback,
                                                user_data);
}

/**
 * gsd_backlight_step_up_finish
 * @backlight: a #GsdBacklight
 * @res: the #GAsyncResult passed to the callback
 * @error: #GError return address
 *
 * Finish an operation started by gsd_backlight_step_up_async(). Will return
 * the value that was actually set (which may be different because of rounding
 * or as multiple set actions were queued up).
 *
 * Please note that a call to gsd_backlight_get_brightness() may not in fact
 * return the same value if further operations to set the value are pending.
 *
 * For simplicity it is also valid to call gsd_backlight_set_brightness_finish()
 * allowing sharing the callback routine for calls to
 * gsd_backlight_set_brightness_async(), gsd_backlight_step_up_async(),
 * gsd_backlight_step_down_async() and gsd_backlight_cycle_up_async().
 *
 * Returns: The brightness in percent that was set.
 **/
gint
gsd_backlight_step_up_finish (GsdBacklight *backlight,
                              GAsyncResult *res,
                              GError **error)
{
        return g_task_propagate_int (G_TASK (res), error);
}

void
gsd_backlight_step_down_async (GsdBacklight *backlight,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
        gint value;

        /* Underflows are handled by gsd_backlight_set_brightness_val_async. */
        value = backlight->brightness_target - backlight->brightness_step;

        gsd_backlight_set_brightness_val_async (backlight,
                                                value,
                                                cancellable,
                                                callback,
                                                user_data);
}

/**
 * gsd_backlight_step_down_finish
 * @backlight: a #GsdBacklight
 * @res: the #GAsyncResult passed to the callback
 * @error: #GError return address
 *
 * Finish an operation started by gsd_backlight_step_down_async(). Will return
 * the value that was actually set (which may be different because of rounding
 * or as multiple set actions were queued up).
 *
 * Please note that a call to gsd_backlight_get_brightness() may not in fact
 * return the same value if further operations to set the value are pending.
 *
 * For simplicity it is also valid to call gsd_backlight_set_brightness_finish()
 * allowing sharing the callback routine for calls to
 * gsd_backlight_set_brightness_async(), gsd_backlight_step_up_async(),
 * gsd_backlight_step_down_async() and gsd_backlight_cycle_up_async().
 *
 * Returns: The brightness in percent that was set.
 **/
gint
gsd_backlight_step_down_finish (GsdBacklight *backlight,
                                GAsyncResult *res,
                                GError **error)
{
        return g_task_propagate_int (G_TASK (res), error);
}

/**
 * gsd_backlight_cycle_up_async
 * @backlight: a #GsdBacklight
 * @cancellable: an optional #GCancellable, NULL to ignore
 * @callback: the #GAsyncReadyCallback invoked for cycle up to be finished
 * @user_data: the #gpointer passed to the callback
 *
 * Start a brightness cycle up operation by gsd_backlight_cycle_up_async().
 * The brightness will be stepped up if it is not already at the maximum.
 * If it is already at the maximum, it will be set to the minimum brightness.
 **/
void
gsd_backlight_cycle_up_async (GsdBacklight *backlight,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
        if (backlight->brightness_target < backlight->brightness_max) {
                gsd_backlight_step_up_async (backlight,
                                             cancellable,
                                             callback,
                                             user_data);
        } else {
                gsd_backlight_set_brightness_val_async (backlight,
                                                        backlight->brightness_min,
                                                        cancellable,
                                                        callback,
                                                        user_data);
        }
}

/**
 * gsd_backlight_cycle_up_finish
 * @backlight: a #GsdBacklight
 * @res: the #GAsyncResult passed to the callback
 * @error: #GError return address
 *
 * Finish an operation started by gsd_backlight_cycle_up_async(). Will return
 * the value that was actually set (which may be different because of rounding
 * or as multiple set actions were queued up).
 *
 * Please note that a call to gsd_backlight_get_brightness() may not in fact
 * return the same value if further operations to set the value are pending.
 *
 * For simplicity it is also valid to call gsd_backlight_set_brightness_finish()
 * allowing sharing the callback routine for calls to
 * gsd_backlight_set_brightness_async(), gsd_backlight_step_up_async(),
 * gsd_backlight_step_down_async() and gsd_backlight_cycle_up_async().
 *
 * Returns: The brightness in percent that was set.
 **/
gint
gsd_backlight_cycle_up_finish (GsdBacklight *backlight,
                               GAsyncResult *res,
                               GError **error)
{
        return g_task_propagate_int (G_TASK (res), error);
}

/**
 * gsd_backlight_get_connector
 * @backlight: a #GsdBacklight
 *
 * Return the connector for the display that is being controlled by the
 * #GsdBacklight object. This connector can be passed to gnome-shell to show
 * the on screen display only on the affected screen.
 *
 * Returns: The connector of the controlled output or NULL if unknown.
 **/
const char*
gsd_backlight_get_connector (GsdBacklight *backlight)
{
        return backlight->backlight_connector;
}

static void
gsd_backlight_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
        GsdBacklight *backlight = GSD_BACKLIGHT (object);

        switch (prop_id) {
        case PROP_BRIGHTNESS:
                g_value_set_int (value, gsd_backlight_get_brightness (backlight));
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
update_mutter_backlight (GsdBacklight *backlight)
{
        GsdDisplayConfig *display_config =
                gnome_settings_bus_get_display_config_proxy ();
        GVariant *backlights = NULL;
        g_autoptr(GVariant) monitors = NULL;
        g_autoptr(GVariant) monitor = NULL;
        gboolean monitor_active = FALSE;

        backlights = gsd_display_config_get_backlight (display_config);
        g_return_if_fail (backlights != NULL);

        g_variant_get (backlights, "(u@aa{sv})",
                       &backlight->backlight_serial,
                       &monitors);
        if (g_variant_n_children (monitors) > 1) {
                g_warning ("Only handling the first out of %lu backlight monitors",
                           g_variant_n_children (monitors));
        }

        if (g_variant_n_children (monitors) > 0) {
                g_variant_get_child (monitors, 0, "@a{sv}", &monitor);

                g_clear_pointer (&backlight->backlight_connector, g_free);
                g_variant_lookup (monitor, "connector", "s",
                                  &backlight->backlight_connector);
                g_variant_lookup (monitor, "active", "b", &monitor_active);
                backlight->builtin_display_disabled = !monitor_active;

                if (g_variant_lookup (monitor, "value", "i", &backlight->brightness_val)) {
                        g_variant_lookup (monitor, "min", "i", &backlight->brightness_min);
                        g_variant_lookup (monitor, "max", "i", &backlight->brightness_max);
                        backlight->initialized = TRUE;
                } else {
                        backlight->brightness_val = -1;
                        backlight->brightness_min = -1;
                        backlight->brightness_max = -1;
                }
        }
}

static void
maybe_update_mutter_backlight (GsdBacklight *backlight)
{
        GsdDisplayConfig *display_config =
                gnome_settings_bus_get_display_config_proxy ();
        g_autofree char *name_owner = NULL;

        if ((name_owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (display_config))))
                update_mutter_backlight (backlight);
}

static void
on_backlight_changed (GsdDisplayConfig *display_config,
                      GParamSpec       *pspec,
                      GsdBacklight     *backlight)
{
        gboolean builtin_display_disabled;

        builtin_display_disabled = backlight->builtin_display_disabled;
        update_mutter_backlight (backlight);

        if (builtin_display_disabled != backlight->builtin_display_disabled)
                g_object_notify_by_pspec (G_OBJECT (backlight), props[PROP_BRIGHTNESS]);
}

static gboolean
gsd_backlight_initable_init (GInitable       *initable,
                             GCancellable    *cancellable,
                             GError         **error)
{
        GsdBacklight *backlight = GSD_BACKLIGHT (initable);
        GsdDisplayConfig *display_config =
                gnome_settings_bus_get_display_config_proxy ();

        if (cancellable != NULL) {
                g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                     "GsdBacklight does not support cancelling initialization.");
                return FALSE;
        }

        if (!display_config) {
                g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                     "GsdBacklight needs org.gnome.Mutter.DisplayConfig to function");
                return FALSE;
        }

        g_signal_connect_object (display_config,
                                 "notify::backlight",
                                 G_CALLBACK (on_backlight_changed),
                                 backlight,
                                 0);

        g_signal_connect_object (display_config,
                                 "notify::g-name-owner",
                                 G_CALLBACK (maybe_update_mutter_backlight),
                                 backlight,
                                 G_CONNECT_SWAPPED);
        maybe_update_mutter_backlight (backlight);

        backlight->brightness_target = backlight->brightness_val;
        backlight->brightness_step =
                MAX(backlight->brightness_step,
                    BRIGHTNESS_STEP_AMOUNT(backlight->brightness_max - backlight->brightness_min + 1));

        g_debug ("Step size for backlight is %i.", backlight->brightness_step);

        return TRUE;
}

static void
gsd_backlight_initable_iface_init (GInitableIface *iface)
{
  iface->init = gsd_backlight_initable_init;
}

static void
gsd_backlight_class_init (GsdBacklightClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsd_backlight_get_property;

        props[PROP_BRIGHTNESS] = g_param_spec_int ("brightness", "The display brightness",
                                                   "The brightness of the internal display in percent.",
                                                   0, 100, 100,
                                                   G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

        g_object_class_install_properties (object_class, PROP_LAST, props);
}

static void
gsd_backlight_init (GsdBacklight *backlight)
{
        backlight->brightness_target = -1;
        backlight->brightness_min = -1;
        backlight->brightness_max = -1;
        backlight->brightness_val = -1;
        backlight->brightness_step = 1;
}

GsdBacklight *
gsd_backlight_new (GError **error)
{
        return GSD_BACKLIGHT (g_initable_new (GSD_TYPE_BACKLIGHT, NULL, error,
                                              NULL));
}

