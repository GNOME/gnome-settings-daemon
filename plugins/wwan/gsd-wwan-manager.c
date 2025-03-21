/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2019 Purism SPC
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
 * Author: Guido Günther <agx@sigxcpu.org>
 *
 */

#include "config.h"

#include <string.h>
#include <locale.h>

#include <gio/gio.h>
#include <glib.h>
#include <glib/gi18n.h>

#include <libmm-glib.h>

#define GCR_API_SUBJECT_TO_CHANGE
#ifdef HAVE_GCR3
#include <gcr/gcr-base.h>
#else
#include <gcr/gcr.h>
#endif

#include "gnome-settings-profile.h"
#include "cc-wwan-device.h"
#include "cc-wwan-errors-private.h"
#include "gsd-wwan-manager.h"


struct _GsdWwanManager
{
        GsdApplication parent;

        guint      start_idle_id;
        gboolean   unlock;
        GSettings *settings;

        /* List of all devices not in ‘devices_to_unlock’ */
        GPtrArray *devices;
        GPtrArray *devices_to_unlock;

        /* Currently shown prompt and device being unlocked */
        GcrPrompt    *prompt;
        CcWwanDevice *unlocking_device;
        GCancellable *cancellable;
        char         *puk_code;      /* Used only for PUK unlock */
        guint         prompt_timeout_id;

        MMManager *mm1;
        gboolean  mm1_running;
};

enum {
        PROP_0,
        PROP_UNLOCK_SIM,
        PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

#define GSD_WWAN_SCHEMA_DIR "org.gnome.settings-daemon.plugins.wwan"
#define GSD_WWAN_SCHEMA_UNLOCK_SIM "unlock-sim"

G_DEFINE_TYPE (GsdWwanManager, gsd_wwan_manager, GSD_TYPE_APPLICATION)

static void wwan_manager_ensure_unlocking   (GsdWwanManager *self);
static void wwan_manager_unlock_device      (CcWwanDevice   *device,
                                             gpointer        user_data);
static void wwan_manager_unlock_required_cb (GsdWwanManager *self,
                                             GParamSpec     *pspec,
                                             CcWwanDevice   *device);

static void
manager_unlock_prompt_new (GsdWwanManager *self,
                           CcWwanDevice   *device,
                           MMModemLock     lock,
                           const char     *msg,
                           gboolean        new_password)
{
        g_autoptr(GError) error = NULL;
        g_autofree gchar *identifier = NULL;
        g_autofree gchar *description = NULL;
        g_autofree gchar *warning = NULL;
        const gchar *message = NULL;
        guint retries;

        identifier = cc_wwan_device_dup_sim_identifier (device);
        g_debug ("Creating new PIN/PUK dialog for SIM %s", identifier);

        if (!self->prompt)
                self->prompt = gcr_system_prompt_open (-1, self->cancellable, &error);

        if (!self->prompt) {
                if (error->code == GCR_SYSTEM_PROMPT_IN_PROGRESS)
                        g_warning ("Another Gcr system prompt is already in progress.");
                else
                        g_warning ("Couldn't create prompt for SIM Code entry: %s", error->message);
                return;
        }

        /* Set up the dialog  */
        if (new_password) {
                gcr_prompt_set_title (self->prompt, _("New PIN for SIM"));
                gcr_prompt_set_continue_label (self->prompt, _("Set"));
        } else {
                gcr_prompt_set_title (self->prompt, _("Unlock SIM card"));
                gcr_prompt_set_continue_label (self->prompt, _("Unlock"));
        }

        gcr_prompt_set_cancel_label (self->prompt, _("Cancel"));
        gcr_prompt_set_password_new (self->prompt, new_password);

        if (lock == MM_MODEM_LOCK_SIM_PIN) {
                if (new_password) {
                        description = g_strdup_printf (_("Please provide a new PIN for SIM card %s"),
                                                       identifier);
                        message = _("Enter a New PIN to unlock your SIM card");
                } else {
                        description = g_strdup_printf (_("Please provide the PIN for SIM card %s"),
                                                       identifier);
                        message = _("Enter PIN to unlock your SIM card");
                }
        } else if (lock == MM_MODEM_LOCK_SIM_PUK) {
                description = g_strdup_printf (_("Please provide the PUK for SIM card %s"),
                                               identifier);
                message = _("Enter PUK to unlock your SIM card");
        } else {
                g_warning ("Unsupported lock type: %u", lock);
                g_clear_object (&self->prompt);
                return;
        }

        gcr_prompt_set_description (self->prompt, description);
        gcr_prompt_set_message (self->prompt, message);

        if (!new_password)
                retries = cc_wwan_device_get_unlock_retries (device, lock);

        if (!new_password && retries != MM_UNLOCK_RETRIES_UNKNOWN) {
                if (msg) {
                        /* msg is already localised */
                        warning = g_strdup_printf (ngettext ("%2$s. You have %1$u try left",
                                                             "%2$s. You have %1$u tries left", retries),
                                                   retries, msg);
                } else {
                        warning = g_strdup_printf (ngettext ("You have %u try left",
                                                             "You have %u tries left", retries),
                                                   retries);
                }
        } else if (msg) {
                warning = g_strdup (msg);
        }

        gcr_prompt_set_warning (self->prompt, warning);

        /* TODO */
        /* if (lock == MM_MODEM_LOCK_SIM_PIN) */
        /*         gcr_prompt_set_choice_label (prompt, _("Automatically unlock this SIM card")); */
}

static gboolean
unlock_device (gpointer user_data)
{
        GsdWwanManager *self;
        CcWwanDevice *device;
        g_autoptr(GTask) task = user_data;
        MMModemLock lock;

        g_assert (G_IS_TASK (task));

        self = g_task_get_task_data (task);
        device = g_task_get_source_object (task);

        g_assert (GSD_IS_WWAN_MANAGER (self));
        g_assert (CC_IS_WWAN_DEVICE (device));

        self->prompt_timeout_id = 0;

        if (g_task_return_error_if_cancelled (task))
                return G_SOURCE_REMOVE;

        lock = cc_wwan_device_get_lock (device);

        if (lock != MM_MODEM_LOCK_SIM_PIN &&
            lock != MM_MODEM_LOCK_SIM_PUK) {
                g_cancellable_cancel (g_task_get_cancellable (task));
                g_task_return_error_if_cancelled (task);
                return G_SOURCE_REMOVE;
        }

        wwan_manager_unlock_device (device, g_steal_pointer (&task));

        return G_SOURCE_REMOVE;
}

static void
wwan_manager_password_sent_cb (GObject      *object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
        GsdWwanManager *self;
        CcWwanDevice *device = (CcWwanDevice *)object;
        g_autoptr(GTask) task = user_data;
        g_autoptr(GError) error = NULL;
        gboolean ret;

        g_assert (CC_IS_WWAN_DEVICE (device));
        g_assert (G_IS_TASK (task));

        self = g_task_get_task_data (task);
        g_assert (GSD_IS_WWAN_MANAGER (self));

        if (self->puk_code)
                ret = cc_wwan_device_send_puk_finish (device, result, &error);
        else
                ret = cc_wwan_device_send_pin_finish (device, result, &error);

        g_clear_pointer (&self->puk_code, gcr_secure_memory_free);

        /* Ask again if a failable error occured */
        if (error &&
            (g_error_matches (error, MM_MOBILE_EQUIPMENT_ERROR, MM_MOBILE_EQUIPMENT_ERROR_INCORRECT_PASSWORD) ||
             g_error_matches (error, MM_MOBILE_EQUIPMENT_ERROR, MM_MOBILE_EQUIPMENT_ERROR_SIM_PUK) ||
             g_error_matches (error, MM_MOBILE_EQUIPMENT_ERROR, MM_MOBILE_EQUIPMENT_ERROR_UNKNOWN))) {
                g_object_set_data_full (G_OBJECT (task), "error",
                                        (gpointer)g_strdup (cc_wwan_error_get_message (error)),
                                        g_free);
                /* ModemManager updates the lock status after some delay.  Wait around 250 milliseconds
                 * so that the values are updated.
                 */
                self->prompt_timeout_id = g_timeout_add (250, unlock_device, g_steal_pointer (&task));

                return;
        }

        if (ret)
                g_task_return_boolean (task, TRUE);
        else
                g_task_return_error (task, g_steal_pointer (&error));

}

static gboolean
wwan_manager_unlock_device_finish (CcWwanDevice  *self,
                                   GAsyncResult  *result,
                                   GError       **error)
{
        return g_task_propagate_boolean (G_TASK (result), error);
}

static const char *
wwan_manager_show_prompt (GsdWwanManager *self,
                          CcWwanDevice   *device,
                          GTask          *task)
{
        g_autoptr(GError) error = NULL;
        const char *code;

        g_assert (GSD_IS_WWAN_MANAGER (self));
        g_assert (CC_IS_WWAN_DEVICE (device));
        g_assert (G_IS_TASK (task));

        if (!self->prompt) {
                g_task_return_new_error (task,
                                         G_IO_ERROR,
                                         G_IO_ERROR_FAILED,
                                         "Failed to create a new prompt");
                return NULL;
        }

        g_set_object (&self->unlocking_device, device);

        /* Irritate user if an empty password is provided */
        do {
                code = gcr_prompt_password_run (self->prompt, self->cancellable, &error);
        } while (code && !*code);

        if (error) {
                g_task_return_error (task, g_steal_pointer (&error));
                return NULL;
        }

        /* User cancelled the dialog */
        if (!code) {
                g_cancellable_cancel (g_task_get_cancellable (task));
                g_task_return_error_if_cancelled (task);
                return NULL;
        }

        return code;
}

static void
wwan_manager_unlock_device (CcWwanDevice *device,
                            gpointer      user_data)
{
        GsdWwanManager *self;
        g_autoptr(GTask) task = user_data;
        GCancellable *cancellable;
        const char *code, *error_msg;
        MMModemLock lock;

        g_assert (CC_IS_WWAN_DEVICE (device));
        g_assert (G_IS_TASK (task));

        self = g_task_get_task_data (task);
        g_assert (GSD_IS_WWAN_MANAGER (self));

        error_msg = g_object_get_data (G_OBJECT (task), "error");
        lock = cc_wwan_device_get_lock (device);
        manager_unlock_prompt_new (self, device, lock, error_msg, FALSE);
        g_object_set_data (G_OBJECT (task), "error", NULL);

        code = wwan_manager_show_prompt (self, device, task);
        if (!code)
                return;

        if (lock == MM_MODEM_LOCK_SIM_PUK) {
                gcr_secure_memory_free (self->puk_code);
                self->puk_code = gcr_secure_memory_strdup (code);

                manager_unlock_prompt_new (self, device, MM_MODEM_LOCK_SIM_PIN, NULL, TRUE);
                code = wwan_manager_show_prompt (self, device, task);
                if (!code)
                        return;
        }

        cancellable = g_task_get_cancellable (task);

        if (lock == MM_MODEM_LOCK_SIM_PIN)
                cc_wwan_device_send_pin (device, code, cancellable,
                                         wwan_manager_password_sent_cb,
                                         g_steal_pointer (&task));
        else if (lock == MM_MODEM_LOCK_SIM_PUK)
                cc_wwan_device_send_puk (device, self->puk_code, code, cancellable,
                                         wwan_manager_password_sent_cb,
                                         g_steal_pointer (&task));
}

static void
wwan_manager_unlock_device_cb (GObject      *object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
        g_autoptr(GsdWwanManager) self = user_data;
        CcWwanDevice *device = (CcWwanDevice *)object;
        g_autoptr(GError) error = NULL;

        g_assert (GSD_IS_WWAN_MANAGER (self));
        g_assert (CC_IS_WWAN_DEVICE (device));
        g_assert (G_IS_TASK (result));

        wwan_manager_unlock_device_finish (device, result, &error);

        /* Move the device from devices to unlock to the list of devices */
        if (g_ptr_array_remove (self->devices_to_unlock, device))
                g_ptr_array_add (self->devices, g_object_ref (device));

        g_clear_pointer (&self->puk_code, gcr_secure_memory_free);
        g_clear_object (&self->prompt);
        g_clear_object (&self->cancellable);
        g_clear_object (&self->unlocking_device);
        g_clear_handle_id (&self->prompt_timeout_id, g_source_remove);

        /* Unlock the next device */
        if (self->devices_to_unlock->len)
                wwan_manager_unlock_required_cb (self, NULL, self->devices_to_unlock->pdata[0]);

        if (error)
                g_debug ("Error unlocking device: %s", error->message);

        if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                g_warning ("Error unlocking device: %s", error->message);
}

static void
wwan_manager_unlock_required_cb (GsdWwanManager *self,
                                 GParamSpec     *pspec,
                                 CcWwanDevice   *device)
{
        MMModemLock lock;

        g_assert (GSD_IS_WWAN_MANAGER (self));
        g_assert (CC_IS_WWAN_DEVICE (device));

        lock = cc_wwan_device_get_lock (device);

        if (lock != MM_MODEM_LOCK_SIM_PIN &&
            lock != MM_MODEM_LOCK_SIM_PUK) {
                g_object_ref (device);

                /* Move the device from devices to unlock to the list of devices */
                if (g_ptr_array_remove (self->devices_to_unlock, device))
                        g_ptr_array_add (self->devices, device);

                /* If the device is the device being unlocked, cancel the process */
                if (device == self->unlocking_device)
                        g_cancellable_cancel (self->cancellable);
        } else if (lock == MM_MODEM_LOCK_SIM_PIN ||
                   lock == MM_MODEM_LOCK_SIM_PUK) {
                g_object_ref (device);

                /* Move the device to devices to unlock from the list of devices */
                if (g_ptr_array_remove (self->devices, device)) {
                        g_ptr_array_add (self->devices_to_unlock, device);
                        wwan_manager_ensure_unlocking (self);
                }
        }
}


static gboolean
device_match_by_object (CcWwanDevice *device, GDBusObject *object)
{
        const char *device_path, *object_path;

        g_return_val_if_fail (G_IS_DBUS_OBJECT (object), FALSE);
        g_return_val_if_fail (CC_IS_WWAN_DEVICE (device), FALSE);

        device_path = cc_wwan_device_get_path (device);
        object_path = mm_object_get_path (MM_OBJECT (object));

        return g_strcmp0 (device_path, object_path) == 0;
}

/*
 * @array: (out) (nullable):
 * @index: (out) (nullable):
 *
 * Returns: %TRUE if found.  %FALSE otherwise
 */
static gboolean
wwan_manager_find_match (GsdWwanManager  *self,
                         GDBusObject     *object,
                         GPtrArray      **array,
                         guint           *index)
{
        GPtrArray *devices = NULL;
        guint i = 0;

        g_return_val_if_fail (G_IS_DBUS_OBJECT (object), FALSE);

        if (g_ptr_array_find_with_equal_func (self->devices,
                                              object,
                                              (GEqualFunc) device_match_by_object,
                                              &i))
                devices = self->devices;
        else if (g_ptr_array_find_with_equal_func (self->devices_to_unlock,
                                                     object,
                                                     (GEqualFunc) device_match_by_object,
                                                     &i))
                devices = self->devices_to_unlock;

        if (index && i >= 0)
                *index = i;
        if (array)
                *array = devices;

        if (devices)
                return TRUE;

        return FALSE;
}


static void
wwan_manager_ensure_unlocking (GsdWwanManager *self)
{
        CcWwanDevice *device;
        GTask *task;

        g_assert (GSD_WWAN_MANAGER (self));

        if (!self->unlock || self->unlocking_device)
                return;

        if (self->devices_to_unlock->len == 0)
                return;

        g_warn_if_fail (!self->cancellable);
        g_clear_object (&self->cancellable);

        device = self->devices_to_unlock->pdata[0];
        self->cancellable = g_cancellable_new ();
        task = g_task_new (device, self->cancellable,
                           wwan_manager_unlock_device_cb,
                           g_object_ref (self));
        g_task_set_task_data (task, g_object_ref (self), g_object_unref);

        wwan_manager_unlock_device (device, task);
}

static void
gsd_wwan_manager_cache_mm_object (GsdWwanManager *self, MMObject *obj)
{
        const gchar *modem_object_path;
        CcWwanDevice *wwan_device;
        MMModemLock lock;

        modem_object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (obj));
        g_return_if_fail (modem_object_path);

        /* This shouldn’t happen, so warn and return if this happen. */
        if (wwan_manager_find_match (self, G_DBUS_OBJECT (obj), NULL, NULL)) {
                g_warning("Device %s already tracked", modem_object_path);
                return;
        }

        g_debug ("Tracking device at: %s", modem_object_path);
        wwan_device = cc_wwan_device_new (MM_OBJECT (obj), NULL);
        lock = cc_wwan_device_get_lock (wwan_device);
        if (lock == MM_MODEM_LOCK_SIM_PIN ||
            lock == MM_MODEM_LOCK_SIM_PUK)
                g_ptr_array_add (self->devices_to_unlock, wwan_device);
        else
                g_ptr_array_add (self->devices, wwan_device);

        g_signal_connect_object (wwan_device, "notify::unlock-required",
                                 G_CALLBACK (wwan_manager_unlock_required_cb),
                                 self, G_CONNECT_SWAPPED);
        wwan_manager_ensure_unlocking (self);
}


static void
object_added_cb (GsdWwanManager *self, GDBusObject *object, GDBusObjectManager *obj_manager)
{
        g_return_if_fail (GSD_IS_WWAN_MANAGER (self));
        g_return_if_fail (G_IS_DBUS_OBJECT_MANAGER (obj_manager));

        gsd_wwan_manager_cache_mm_object (self, MM_OBJECT(object));
}


static void
object_removed_cb (GsdWwanManager     *self,
                   GDBusObject        *object,
                   GDBusObjectManager *obj_manager)
{
        CcWwanDevice *device;
        GPtrArray *devices;
        guint index;

        g_return_if_fail (GSD_IS_WWAN_MANAGER (self));
        g_return_if_fail (G_IS_DBUS_OBJECT_MANAGER (obj_manager));

        if (!wwan_manager_find_match (self, object, &devices, &index))
                g_return_if_reached ();

        device = g_ptr_array_index (devices, index);

        g_ptr_array_remove_index (devices, index);

        if (device == self->unlocking_device)
                g_cancellable_cancel (self->cancellable);
}


static void
mm1_name_owner_changed_cb (GDBusObjectManagerClient *client, GParamSpec *pspec, GsdWwanManager *self)
{
        g_autofree gchar *name_owner = NULL;

        name_owner = g_dbus_object_manager_client_get_name_owner (client);
        self->mm1_running = !!name_owner;
        g_debug ("mm name owned: %d", self->mm1_running);

        if (!self->mm1_running) {
                /* Drop all devices when MM goes away */
                g_ptr_array_set_size (self->devices, 0);
                g_ptr_array_set_size (self->devices_to_unlock, 0);

                g_clear_object (&self->prompt);
                g_clear_pointer (&self->puk_code, gcr_secure_memory_free);
                g_clear_object (&self->unlocking_device);

                return;
        }
}


static void
get_all_modems (GsdWwanManager *self)
{
        GList *list, *l;

        g_return_if_fail (MM_IS_MANAGER (self->mm1));

        list = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (self->mm1));
        for (l = list; l != NULL; l = l->next)
                gsd_wwan_manager_cache_mm_object (self, MM_OBJECT(l->data));
        g_list_free_full (list, g_object_unref);
}


static void
mm1_manager_new_cb (GDBusConnection *connection, GAsyncResult *res, GsdWwanManager *self)
{
        g_autoptr(GError) error = NULL;

        self->mm1 = mm_manager_new_finish (res, &error);
        if (self->mm1) {
                /* Listen for added/removed modems */
                g_signal_connect_object (self->mm1,
                                         "object-added",
                                         G_CALLBACK (object_added_cb),
                                         self,
                                         G_CONNECT_SWAPPED);

                g_signal_connect_object (self->mm1,
                                         "object-removed",
                                         G_CALLBACK (object_removed_cb),
                                         self,
                                         G_CONNECT_SWAPPED);

                /* Listen for name owner changes */
                g_signal_connect (self->mm1,
                                  "notify::name-owner",
                                  G_CALLBACK (mm1_name_owner_changed_cb),
                                  self);

                /* Handle all modems already known to MM */
                get_all_modems (self);
        } else {
                g_warning ("Error connecting to D-Bus: %s", error->message);
        }
}


static void
set_modem_manager (GsdWwanManager *self)
{
        GDBusConnection *system_bus;
        g_autoptr(GError) error = NULL;

        system_bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (system_bus) {
                mm_manager_new (system_bus,
                                G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START,
                                NULL,
                                (GAsyncReadyCallback) mm1_manager_new_cb,
                                self);
                g_object_unref (system_bus);
        } else {
                g_warning ("Error connecting to system D-Bus: %s", error->message);
        }
}


static gboolean
start_wwan_idle_cb (GsdWwanManager *self)
{
        g_debug ("Idle starting wwan manager");
        gnome_settings_profile_start (NULL);

        g_return_val_if_fail(GSD_IS_WWAN_MANAGER (self), FALSE);
        self->settings = g_settings_new (GSD_WWAN_SCHEMA_DIR);
        g_settings_bind (self->settings, "unlock-sim", self, "unlock-sim", G_SETTINGS_BIND_GET);

        set_modem_manager (self);
        gnome_settings_profile_end (NULL);
        self->start_idle_id = 0;

        return FALSE;
}

static void
gsd_wwan_manager_startup (GApplication *app)
{
        GsdWwanManager *self = GSD_WWAN_MANAGER (app);

        g_debug ("Starting wwan manager");
        g_return_if_fail (GSD_IS_WWAN_MANAGER (self));

        gnome_settings_profile_start (NULL);
        self->start_idle_id = g_idle_add ((GSourceFunc) start_wwan_idle_cb, self);
        g_source_set_name_by_id (self->start_idle_id, "[gnome-settings-daemon] start_wwan_idle_cb");

        G_APPLICATION_CLASS (gsd_wwan_manager_parent_class)->startup (app);

        gnome_settings_profile_end (NULL);
}

static void
gsd_wwan_manager_shutdown (GApplication *app)
{
        GsdWwanManager *manager = GSD_WWAN_MANAGER (app);

        g_debug ("Stopping wwan manager");

        g_clear_handle_id (&manager->start_idle_id, g_source_remove);

        G_APPLICATION_CLASS (gsd_wwan_manager_parent_class)->shutdown (app);
}


static void
gsd_wwan_manager_set_unlock_sim (GsdWwanManager *self, gboolean unlock)
{
        if (self->unlock == unlock)
                return;

        self->unlock = unlock;

        /*
         * XXX: Should the devices in ‘self->devices’ be moved to
         * ‘self->devices_to_unlock’ if required?  Otherwise, no prompt
         * will be shown for devices the user explicitly cancelled
         * unlock prompt.
         */
        /* Unlock the first device if no device is being unlocked.  Unlocking
         * the rest will be handled appropriately after this is finished. */
        if (self->unlock && self->devices_to_unlock->len > 0 && !self->unlocking_device)
                wwan_manager_unlock_required_cb (self, NULL,
                                                 self->devices_to_unlock->pdata[0]);

        g_object_notify_by_pspec (G_OBJECT (self), props[PROP_UNLOCK_SIM]);
}


static void
gsd_wwan_manager_set_property (GObject        *object,
                               guint           prop_id,
                               const GValue   *value,
                               GParamSpec     *pspec)
{
        GsdWwanManager *self = GSD_WWAN_MANAGER (object);

        switch (prop_id) {
        case PROP_UNLOCK_SIM:
                gsd_wwan_manager_set_unlock_sim (self, g_value_get_boolean (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsd_wwan_manager_get_property (GObject        *object,
                               guint           prop_id,
                               GValue         *value,
                               GParamSpec     *pspec)
{
        GsdWwanManager *self = GSD_WWAN_MANAGER (object);

        switch (prop_id) {
        case PROP_UNLOCK_SIM:
                g_value_set_boolean (value, self->unlock);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsd_wwan_manager_dispose (GObject *object)
{
        GsdWwanManager *self = GSD_WWAN_MANAGER (object);

        if (self->mm1) {
                self->mm1_running = FALSE;
                g_clear_object (&self->mm1);
        }

        if (self->cancellable)
                g_cancellable_cancel (self->cancellable);
        g_clear_object (&self->cancellable);
        g_clear_handle_id (&self->prompt_timeout_id, g_source_remove);
        g_clear_object (&self->unlocking_device);
        g_clear_pointer (&self->puk_code, gcr_secure_memory_free);
        g_clear_object (&self->prompt);

        g_clear_pointer (&self->devices, g_ptr_array_unref);
        g_clear_pointer (&self->devices_to_unlock, g_ptr_array_unref);
        g_clear_object (&self->settings);

        G_OBJECT_CLASS (gsd_wwan_manager_parent_class)->dispose (object);
}

static void
gsd_wwan_manager_class_init (GsdWwanManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GApplicationClass *application_class = G_APPLICATION_CLASS (klass);

        object_class->get_property = gsd_wwan_manager_get_property;
        object_class->set_property = gsd_wwan_manager_set_property;
        object_class->dispose = gsd_wwan_manager_dispose;

        application_class->startup = gsd_wwan_manager_startup;
        application_class->shutdown = gsd_wwan_manager_shutdown;

        props[PROP_UNLOCK_SIM] =
                g_param_spec_boolean ("unlock-sim",
                                      "unlock-sim",
                                      "Whether to unlock new sims right away",
                                      FALSE,
                                      G_PARAM_READWRITE |
                                      G_PARAM_EXPLICIT_NOTIFY |
                                      G_PARAM_STATIC_STRINGS);
        g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}

static void
gsd_wwan_manager_init (GsdWwanManager *self)
{
        self->devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
        self->devices_to_unlock = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}
