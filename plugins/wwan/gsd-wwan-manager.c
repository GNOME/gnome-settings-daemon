/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Purism SPC
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



typedef struct
{
        guint      start_idle_id;
        gboolean   unlock;
        GSettings *settings;

        GPtrArray *devices;

        MMManager *mm1;
        gboolean  mm1_running;
        gulong mm1_object_added_signal_id;
        gulong mm1_object_removed_signal_id;
} GsdWwanManagerPrivate;

typedef struct _GsdWwanManager
{
        GObject parent;
} GsdWwanManager;


enum {
        PROP_0,
        PROP_UNLOCK_SIM,
        PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

#define GSD_WWAN_SCHEMA_DIR "org.gnome.settings-daemon.plugins.wwan"
#define GSD_WWAN_SCHEMA_UNLOCK_SIM "unlock-sim"

static void     gsd_wwan_manager_class_init  (GsdWwanManagerClass *klass);
static void     gsd_wwan_manager_init        (GsdWwanManager      *wwan_manager);

G_DEFINE_TYPE_WITH_PRIVATE (GsdWwanManager, gsd_wwan_manager, G_TYPE_OBJECT)

/* The plugin's manager object */
static gpointer manager_object = NULL;


static void
unlock_sim_cb (GsdWwanDevice *device, gpointer data)
{
        GsdWwanManager *self = GSD_WWAN_MANAGER (data);
        GsdWwanManagerPrivate *priv = gsd_wwan_manager_get_instance_private (self);

        g_return_if_fail (GSD_IS_WWAN_MANAGER (self));
        g_return_if_fail (GSD_IS_WWAN_DEVICE (device));

        if (!priv->unlock)
                return;

        gsd_wwan_pinentry_unlock_sim (device, NULL);
}


static void
gsd_wwan_manager_cache_mm_object (GsdWwanManager *self, MMObject *obj)
{
        GsdWwanManagerPrivate *priv = gsd_wwan_manager_get_instance_private (self);
        const gchar *modem_object_path;
        GsdWwanDevice *device;

        modem_object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (obj));
        g_return_if_fail (modem_object_path);

        g_debug ("Tracking device at: %s", modem_object_path);
        device = gsd_wwan_device_new (MM_OBJECT (obj));
        g_signal_connect (device,
                          "sim-needs-unlock",
                          G_CALLBACK (unlock_sim_cb),
                          self);
        g_ptr_array_add (priv->devices, device);
}


static void
object_added_cb (GsdWwanManager *self, GDBusObject *object, GDBusObjectManager *obj_manager)
{
        g_return_if_fail (GSD_IS_WWAN_MANAGER (self));
        g_return_if_fail (G_IS_DBUS_OBJECT_MANAGER (obj_manager));

        gsd_wwan_manager_cache_mm_object (self, MM_OBJECT(object));
}


static gboolean
device_match_by_object (GsdWwanDevice *device, GDBusObject *object)
{
        g_return_val_if_fail (G_IS_DBUS_OBJECT (object), FALSE);
        g_return_val_if_fail (GSD_IS_WWAN_DEVICE (device), FALSE);

        return object == G_DBUS_OBJECT (gsd_wwan_device_get_mm_object (device));
}


static void
object_removed_cb (GsdWwanManager *self, GDBusObject *object, GDBusObjectManager *obj_manager)
{
        GsdWwanManagerPrivate *priv = gsd_wwan_manager_get_instance_private (self);
        guint index;

        g_return_if_fail (GSD_IS_WWAN_MANAGER (self));
        g_return_if_fail (G_IS_DBUS_OBJECT_MANAGER (obj_manager));

        if (g_ptr_array_find_with_equal_func (priv->devices,
                                              object,
                                              (GEqualFunc) device_match_by_object,
                                              &index)) {
                g_ptr_array_remove_index_fast (priv->devices, index);
        } else {
                g_warning ("Trying to remove an inexistent object at %s",
                           g_dbus_object_get_object_path (object));
        }
}


static void
mm1_name_owner_changed_cb (GDBusObjectManagerClient *client, GParamSpec *pspec, GsdWwanManager *self)
{
        g_autofree gchar *name_owner = NULL;
        GsdWwanManagerPrivate *priv = gsd_wwan_manager_get_instance_private (self);
        GList *list, *l;

        name_owner = g_dbus_object_manager_client_get_name_owner (client);
        priv->mm1_running = !!name_owner;

        g_debug ("mm name owned: %d", priv->mm1_running);

        /* Listen for new modems */
        priv->mm1_object_added_signal_id =
                g_signal_connect_swapped (priv->mm1,
                                          "object-added",
                                          G_CALLBACK (object_added_cb),
                                          self);

        priv->mm1_object_removed_signal_id =
                g_signal_connect_swapped (priv->mm1,
                                          "object-removed",
                                          G_CALLBACK (object_removed_cb),
                                          self);

        /* Handle existing modems */
        list = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (priv->mm1));
        for (l = list; l != NULL; l = l->next)
                gsd_wwan_manager_cache_mm_object (self, MM_OBJECT(l->data));
        g_list_free_full (list, g_object_unref);
}


static void
mm1_manager_new_cb (GDBusConnection *connection, GAsyncResult *res, GsdWwanManager *self)
{
        GError *error = NULL;
        GsdWwanManagerPrivate *priv = gsd_wwan_manager_get_instance_private (self);

        priv->mm1 = mm_manager_new_finish (res, &error);
        if (priv->mm1) {
                g_signal_connect (priv->mm1,
                                  "notify::name-owner",
                                  G_CALLBACK (mm1_name_owner_changed_cb),
                                  self);
                mm1_name_owner_changed_cb (G_DBUS_OBJECT_MANAGER_CLIENT (priv->mm1), NULL, self);
        } else {
                g_warning ("Error connecting to D-Bus: %s", error->message);
                g_clear_error (&error);
        }
}


static void
set_modem_manager (GsdWwanManager *self)
{
        GDBusConnection *system_bus;
        GError *error = NULL;

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
                g_clear_error (&error);
        }
}


static gboolean
start_wwan_idle_cb (GsdWwanManager *self)
{
        GsdWwanManagerPrivate *priv = gsd_wwan_manager_get_instance_private (self);

        g_debug ("Idle starting wwan manager");
        gnome_settings_profile_start (NULL);

        g_return_val_if_fail(GSD_IS_WWAN_MANAGER (self), FALSE);
        priv->settings = g_settings_new (GSD_WWAN_SCHEMA_DIR);
        g_settings_bind (priv->settings, "unlock-sim", self, "unlock-sim", G_SETTINGS_BIND_GET);

        set_modem_manager (self);
        gnome_settings_profile_end (NULL);
        priv->start_idle_id = 0;

        return FALSE;
}

gboolean
gsd_wwan_manager_start (GsdWwanManager *self,
                        GError        **error)
{
        GsdWwanManagerPrivate *priv = gsd_wwan_manager_get_instance_private (self);

        g_debug ("Starting wwan manager");
        g_return_val_if_fail(GSD_IS_WWAN_MANAGER (self), FALSE);

        gnome_settings_profile_start (NULL);
        priv->start_idle_id = g_idle_add ((GSourceFunc) start_wwan_idle_cb, self);
        g_source_set_name_by_id (priv->start_idle_id, "[gnome-settings-daemon] start_wwan_idle_cb");

        gnome_settings_profile_end (NULL);
        return TRUE;
}

void
gsd_wwan_manager_stop (GsdWwanManager *self)
{
        g_debug ("Stopping wwan manager");
}

static void
gsd_wwan_manager_set_property (GObject        *object,
                               guint           prop_id,
                               const GValue   *value,
                               GParamSpec     *pspec)
{
        GsdWwanManager *self = GSD_WWAN_MANAGER (object);
        GsdWwanManagerPrivate *priv = gsd_wwan_manager_get_instance_private (self);

        switch (prop_id) {
        case PROP_UNLOCK_SIM:
                priv->unlock = g_value_get_boolean (value);
                g_object_notify_by_pspec (G_OBJECT (self), props[PROP_UNLOCK_SIM]);
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
        GsdWwanManagerPrivate *priv = gsd_wwan_manager_get_instance_private (self);

        switch (prop_id) {
        case PROP_UNLOCK_SIM:
                g_value_set_boolean (value, priv->unlock);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gsd_wwan_manager_constructor (GType                  type,
                              guint                  n_construct_properties,
                              GObjectConstructParam *construct_properties)
{
        GsdWwanManager      *wwan_manager;

        wwan_manager = GSD_WWAN_MANAGER (G_OBJECT_CLASS (gsd_wwan_manager_parent_class)->constructor (type,
                                                                                                      n_construct_properties,
                                                                                                      construct_properties));

        return G_OBJECT (wwan_manager);
}


static void
gsd_wwan_manager_dispose (GObject *object)
{
        GsdWwanManager *manager = GSD_WWAN_MANAGER (object);
        GsdWwanManagerPrivate *priv = gsd_wwan_manager_get_instance_private (manager);

        if (priv->mm1) {
                g_signal_handler_disconnect (priv->mm1,
                                             priv->mm1_object_added_signal_id);
                priv->mm1_object_added_signal_id = 0;
                g_signal_handler_disconnect (priv->mm1,
                                             priv->mm1_object_removed_signal_id);
                priv->mm1_object_removed_signal_id = 0;
                priv->mm1_running = FALSE;
        }

        if (priv->devices) {
                g_ptr_array_free (priv->devices, TRUE);
                priv->devices = NULL;
        }

        G_OBJECT_CLASS (gsd_wwan_manager_parent_class)->dispose (object);
}


static void
gsd_wwan_manager_class_init (GsdWwanManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsd_wwan_manager_get_property;
        object_class->set_property = gsd_wwan_manager_set_property;
        object_class->constructor = gsd_wwan_manager_constructor;
        object_class->dispose = gsd_wwan_manager_dispose;

        props[PROP_UNLOCK_SIM] =
                g_param_spec_boolean ("unlock-sim",
                                      "unlock-sim",
                                      "Whether to unlock new sims right away",
                                      TRUE,
                                      G_PARAM_READWRITE |
                                      G_PARAM_EXPLICIT_NOTIFY |
                                      G_PARAM_STATIC_STRINGS);
        g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}

static void
gsd_wwan_manager_init (GsdWwanManager *self)
{
        GsdWwanManagerPrivate *priv = gsd_wwan_manager_get_instance_private (self);
        priv->devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
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
