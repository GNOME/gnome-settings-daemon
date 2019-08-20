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

#include "gnome-settings-profile.h"
#include "gsd-wwan-device.h"
#include "gsd-wwan-manager.h"
#include "gsd-wwan-pinentry.h"


struct _GsdWwanManager
{
        GObject parent;

        GDBusNodeInfo   *introspection_data;
        GDBusConnection *connection;
        GCancellable    *cancellable;
        guint            name_id;

        guint      start_idle_id;
        gboolean   unlock;
        GSettings *settings;

        GPtrArray *devices;

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

#define GSD_DBUS_NAME "org.gnome.SettingsDaemon"
#define GSD_DBUS_PATH "/org/gnome/SettingsDaemon"
#define GSD_DBUS_BASE_INTERFACE "org.gnome.SettingsDaemon"

#define GSD_WWAN_DBUS_NAME GSD_DBUS_NAME ".Wwan"
#define GSD_WWAN_DBUS_PATH GSD_DBUS_PATH "/Wwan"

static const gchar introspection_xml[] =
"<node>"
"  <interface name='org.gnome.SettingsDaemon.Wwan'>"
"    <method name='UnlockModem'>"
"      <arg type='s' name='modem-path' direction='in'/>"
"    </method>"
"  </interface>"
"</node>";

G_DEFINE_TYPE (GsdWwanManager, gsd_wwan_manager, G_TYPE_OBJECT)

/* The plugin's manager object */
static gpointer manager_object = NULL;


static void
unlock_sim_cb (GsdWwanManager *self, GsdWwanDevice *device)
{
        g_return_if_fail (GSD_IS_WWAN_MANAGER (self));
        g_return_if_fail (GSD_IS_WWAN_DEVICE (device));

        if (!self->unlock)
                return;

        gsd_wwan_pinentry_unlock_sim (device, NULL);
}


static GsdWwanDevice *
lookup_device_with_modem_path (GsdWwanManager *self,
                               const gchar    *modem_path)
{
        GsdWwanDevice *device;
        MMObject *mm_object;
        guint i;

        for (i = 0; i < self->devices->len; i++)
                {
                        device = self->devices->pdata[i];
                        mm_object = gsd_wwan_device_get_mm_object (device);

                        if (g_strcmp0 (modem_path, mm_object_get_path (mm_object)) == 0)
                                return device;
                }

        return NULL;
}

static gboolean
unlock_device_with_modem_path (GsdWwanManager  *self,
                               const gchar     *modem_path,
                               GError         **error)
{
        GsdWwanDevice *device;

        g_return_val_if_fail (GSD_IS_WWAN_MANAGER (self), FALSE);
        g_return_val_if_fail (modem_path || *modem_path, FALSE);

        device = lookup_device_with_modem_path (self, modem_path);

        if (!device)
                {
                        gchar *msg = g_strdup_printf ("No Modem with path '%s' found in cache", modem_path);
                        *error = g_error_new_literal (G_IO_ERROR,
                                                      G_IO_ERROR_NOT_FOUND,
                                                      msg);
                        g_free (msg);

                        return FALSE;
                }

        if (gsd_wwan_device_needs_unlock (device))
                gsd_wwan_pinentry_unlock_sim (device, NULL);

        return TRUE;
}

static gboolean
device_match_by_object (GsdWwanDevice *device, GDBusObject *object)
{
        g_return_val_if_fail (G_IS_DBUS_OBJECT (object), FALSE);
        g_return_val_if_fail (GSD_IS_WWAN_DEVICE (device), FALSE);

        return object == G_DBUS_OBJECT (gsd_wwan_device_get_mm_object (device));
}


static void
gsd_wwan_manager_cache_mm_object (GsdWwanManager *self, MMObject *obj)
{
        const gchar *modem_object_path;
        GsdWwanDevice *device;

        modem_object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (obj));
        g_return_if_fail (modem_object_path);

        if (g_ptr_array_find_with_equal_func (self->devices,
                                              obj,
                                              (GEqualFunc) device_match_by_object,
                                              NULL)) {
                g_debug("Device %s already tracked", modem_object_path);
                return;
        }

        g_debug ("Tracking device at: %s", modem_object_path);
        device = gsd_wwan_device_new (MM_OBJECT (obj));
        g_signal_connect_swapped (device,
                                  "sim-needs-unlock",
                                  G_CALLBACK (unlock_sim_cb),
                                  self);
        g_ptr_array_add (self->devices, device);
}


static void
object_added_cb (GsdWwanManager *self, GDBusObject *object, GDBusObjectManager *obj_manager)
{
        g_return_if_fail (GSD_IS_WWAN_MANAGER (self));
        g_return_if_fail (G_IS_DBUS_OBJECT_MANAGER (obj_manager));

        gsd_wwan_manager_cache_mm_object (self, MM_OBJECT(object));
}


static void
object_removed_cb (GsdWwanManager *self, GDBusObject *object, GDBusObjectManager *obj_manager)
{
        guint index;

        g_return_if_fail (GSD_IS_WWAN_MANAGER (self));
        g_return_if_fail (G_IS_DBUS_OBJECT_MANAGER (obj_manager));

        if (g_ptr_array_find_with_equal_func (self->devices,
                                              object,
                                              (GEqualFunc) device_match_by_object,
                                              &index)) {
                g_ptr_array_remove_index_fast (self->devices, index);
        }
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
                if (self->devices->len) {
                        g_ptr_array_set_size (self->devices, 0);
                }
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


static void
handle_method_call (GDBusConnection       *connection,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *interface_name,
                    const gchar           *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               user_data)
{
        GsdWwanManager *self = (GsdWwanManager *)user_data;
        GError *error = NULL;

        if (g_strcmp0 (method_name, "UnlockModem") == 0) {
                const char *modem_path;
                g_variant_get (parameters, "(&s)", &modem_path);
                if (!unlock_device_with_modem_path (self, modem_path, &error))
                        g_dbus_method_invocation_take_error (invocation, error);
                else
                        g_dbus_method_invocation_return_value (invocation, NULL);
        }
}


static const GDBusInterfaceVTable interface_vtable =
{
        handle_method_call,
        NULL,
        NULL
};

static void
on_bus_gotten (GObject        *source_object,
               GAsyncResult   *res,
               GsdWwanManager *self)
{
        GError *error = NULL;

        self->connection = g_bus_get_finish (res, &error);
        if (self->connection == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Could not get session bus: %s", error->message);
                g_error_free (error);
                return;
        }

        g_dbus_connection_register_object (self->connection,
                                           GSD_WWAN_DBUS_PATH,
                                           self->introspection_data->interfaces[0],
                                           &interface_vtable,
                                           self,
                                           NULL,
                                           NULL);

        self->name_id = g_bus_own_name_on_connection (self->connection,
                                                      GSD_WWAN_DBUS_NAME,
                                                      G_BUS_NAME_OWNER_FLAGS_NONE,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL);
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

        self->introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
        g_assert (self->introspection_data != NULL);

        /* Start process of owning a D-Bus name */
        g_bus_get (G_BUS_TYPE_SESSION,
                   self->cancellable,
                   (GAsyncReadyCallback) on_bus_gotten,
                   self);

        return FALSE;
}

gboolean
gsd_wwan_manager_start (GsdWwanManager *self,
                        GError        **error)
{
        g_debug ("Starting wwan manager");
        g_return_val_if_fail(GSD_IS_WWAN_MANAGER (self), FALSE);

        gnome_settings_profile_start (NULL);
        self->start_idle_id = g_idle_add ((GSourceFunc) start_wwan_idle_cb, self);
        g_source_set_name_by_id (self->start_idle_id, "[gnome-settings-daemon] start_wwan_idle_cb");

        gnome_settings_profile_end (NULL);
        return TRUE;
}

void
gsd_wwan_manager_stop (GsdWwanManager *self)
{
        g_debug ("Stopping wwan manager");

        if (self->name_id != 0) {
                g_bus_unown_name (self->name_id);
                self->name_id = 0;
        }

        g_clear_pointer (&self->introspection_data, g_dbus_node_info_unref);
        g_clear_object (&self->connection);

        if (self->cancellable) {
                g_cancellable_cancel (self->cancellable);
                g_clear_object (&self->cancellable);
        }
}


static void
unlock_all (GsdWwanDevice *device, GsdWwanManager *self)
{
        if (gsd_wwan_device_needs_unlock (device))
                unlock_sim_cb (self, device);
}


static void
gsd_wwan_manager_set_unlock_sim (GsdWwanManager *self, gboolean unlock)
{
        if (self->unlock == unlock)
                return;

        self->unlock = unlock;

        if (self->unlock) {
                g_ptr_array_foreach (self->devices,
                                     (GFunc) unlock_all,
                                     self);
        }

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
        g_clear_pointer (&self->devices, g_ptr_array_unref);
        g_clear_object (&self->settings);

        G_OBJECT_CLASS (gsd_wwan_manager_parent_class)->dispose (object);
}

static void
gsd_wwan_manager_class_init (GsdWwanManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsd_wwan_manager_get_property;
        object_class->set_property = gsd_wwan_manager_set_property;
        object_class->dispose = gsd_wwan_manager_dispose;

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
}


GsdWwanManager *
gsd_wwan_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_WWAN_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_WWAN_MANAGER (manager_object);
}
