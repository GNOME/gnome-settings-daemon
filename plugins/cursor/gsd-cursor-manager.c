/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011-2013 Bastien Nocera <hadess@hadess.net>
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

#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include <X11/Xatom.h>
#include <X11/extensions/Xfixes.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-idle-monitor.h>

#include "gnome-settings-bus.h"
#include "gnome-settings-profile.h"
#include "gsd-cursor-manager.h"
#include "gsd-input-helper.h"

#define XFIXES_CURSOR_HIDING_MAJOR 4

#define GSD_CURSOR_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_CURSOR_MANAGER, GsdCursorManagerPrivate))

#define GSD_CURSOR_DBUS_NAME "org.gnome.SettingsDaemon.Cursor"
#define GSD_CURSOR_DBUS_PATH "/org/gnome/SettingsDaemon/Cursor"
#define GSD_CURSOR_DBUS_INTERFACE "org.gnome.SettingsDaemon.Cursor"

struct GsdCursorManagerPrivate
{
        guint added_id;
        guint removed_id;
        guint changed_id;
        gboolean cursor_shown;
        GHashTable *monitors;

        gboolean show_osk;
        guint dbus_own_name_id;
        guint dbus_register_object_id;
        GCancellable *cancellable;
        GDBusConnection *dbus_connection;
        GDBusNodeInfo *dbus_introspection;
};

static const gchar introspection_xml[] =
        "<node>"
        "  <interface name='org.gnome.SettingsDaemon.Cursor'>"
        "    <property name='ShowOSK' type='b' access='read'/>"
        "  </interface>"
        "</node>";

static void     gsd_cursor_manager_class_init  (GsdCursorManagerClass *klass);
static void     gsd_cursor_manager_init        (GsdCursorManager      *cursor_manager);

G_DEFINE_TYPE (GsdCursorManager, gsd_cursor_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static gboolean add_all_devices (GsdCursorManager *manager, GdkDevice *exception, GError **error);

static void
set_cursor_visibility (GsdCursorManager *manager,
                       gboolean          visible)
{
        GdkWindow *root;
        Display *xdisplay;

        g_debug ("Attempting to %s the cursor", visible ? "show" : "hide");

        if (manager->priv->cursor_shown == visible)
                return;

        gdk_error_trap_push ();

        root = gdk_screen_get_root_window (gdk_screen_get_default ());
        xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

        if (visible)
                XFixesShowCursor (xdisplay, GDK_WINDOW_XID (root));
        else
                XFixesHideCursor (xdisplay, GDK_WINDOW_XID (root));

        if (gdk_error_trap_pop ()) {
                g_warning ("An error occurred trying to %s the cursor",
                           visible ? "show" : "hide");
        }

        manager->priv->cursor_shown = visible;
}

static void
set_osk_enabled (GsdCursorManager *manager,
                 gboolean          enabled)
{
        GError *error = NULL;
        GVariantBuilder *builder;

        if (manager->priv->show_osk == enabled)
                return;

        g_debug ("Switching the OSK to %s", enabled ? "enabled" : "disabled");
        manager->priv->show_osk = enabled;

        if (manager->priv->dbus_connection == NULL)
                return;

        builder = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);
        g_variant_builder_add (builder,
                               "{sv}",
                               "ShowOSK",
                               g_variant_new_boolean (enabled));
        g_dbus_connection_emit_signal (manager->priv->dbus_connection,
                                       NULL,
                                       GSD_CURSOR_DBUS_PATH,
                                       "org.freedesktop.DBus.Properties",
                                       "PropertiesChanged",
                                       g_variant_new ("(sa{sv}as)",
                                                      GSD_CURSOR_DBUS_INTERFACE,
                                                      builder,
                                                      NULL),
                                       &error);

        if (error)
                g_warning ("Error while emitting D-Bus signal: %s", error->message);
}

static void
monitor_became_active (GnomeIdleMonitor *monitor,
                       guint             watch_id,
                       gpointer          user_data)
{
        GdkDevice *device;
        GsdCursorManager *manager = GSD_CURSOR_MANAGER (user_data);

        /* Oh, so you're active? */
        g_object_get (G_OBJECT (monitor), "device", &device, NULL);
        g_debug ("Device %d '%s' became active", gdk_x11_device_get_id (device), gdk_device_get_name (device));
        set_cursor_visibility (manager,
                               gdk_device_get_source (device) != GDK_SOURCE_TOUCHSCREEN);
        set_osk_enabled (manager,
                         gdk_device_get_source (device) == GDK_SOURCE_TOUCHSCREEN);

        /* Remove the device from the watch */
        g_hash_table_remove (manager->priv->monitors, device);

        /* Make sure that all the other devices are watched
         * (but not the one we just stopped monitoring */
        add_all_devices (manager, device, NULL);

        g_object_unref (device);
}

static gboolean
add_device (GdkDeviceManager *device_manager,
            GdkDevice        *device,
            GsdCursorManager *manager,
            GError          **error)
{
        GnomeIdleMonitor *monitor;

        if (g_hash_table_lookup (manager->priv->monitors, device) != NULL)
                return TRUE;
        if (gdk_device_get_device_type (device) != GDK_DEVICE_TYPE_SLAVE)
                return TRUE;
        if (gdk_device_get_source (device) == GDK_SOURCE_KEYBOARD)
                return TRUE;
        if (strstr (gdk_device_get_name (device), "XTEST") != NULL)
                return TRUE;

        /* Create IdleMonitors for each pointer device */
        monitor = gnome_idle_monitor_new_for_device (device, error);
        if (!monitor)
                return FALSE;
        g_hash_table_insert (manager->priv->monitors,
                             device,
                             monitor);
        gnome_idle_monitor_add_user_active_watch (monitor,
                                                  monitor_became_active,
                                                  manager,
                                                  NULL);

        return TRUE;
}

static void
device_added_cb (GdkDeviceManager *device_manager,
                 GdkDevice        *device,
                 GsdCursorManager *manager)
{
        add_device (device_manager, device, manager, NULL);
}

static void
device_removed_cb (GdkDeviceManager *device_manager,
                   GdkDevice        *device,
                   GsdCursorManager *manager)
{
        g_hash_table_remove (manager->priv->monitors,
                             device);
}

static void
device_changed_cb (GdkDeviceManager *device_manager,
                   GdkDevice        *device,
                   GsdCursorManager *manager)
{
        if (gdk_device_get_device_type (device) == GDK_DEVICE_TYPE_FLOATING)
                device_removed_cb (device_manager, device, manager);
        else
                device_added_cb (device_manager, device, manager);
}

static gboolean
supports_xfixes (void)
{
        gint op_code, event, error;

        return XQueryExtension (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                                "XFIXES",
                                &op_code,
                                &event,
                                &error);
}

static gboolean
supports_cursor_xfixes (void)
{
        int major = XFIXES_CURSOR_HIDING_MAJOR;
        int minor = 0;

        gdk_error_trap_push ();

        if (!supports_xfixes ()) {
                gdk_error_trap_pop_ignored ();
                return FALSE;
        }

        if (!XFixesQueryVersion (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), &major, &minor)) {
                gdk_error_trap_pop_ignored ();
                return FALSE;
        }
        gdk_error_trap_pop_ignored ();

        if (major >= XFIXES_CURSOR_HIDING_MAJOR)
                return TRUE;

        return FALSE;
}

static gboolean
add_all_devices (GsdCursorManager *manager,
                 GdkDevice        *exception,
                 GError          **error)
{
        GdkDeviceManager *device_manager;
        GList *devices, *l;
        gboolean ret = TRUE;

        device_manager = gdk_display_get_device_manager (gdk_display_get_default ());
        devices = gdk_device_manager_list_devices (device_manager, GDK_DEVICE_TYPE_SLAVE);
        for (l = devices; l != NULL; l = l->next) {
                GdkDevice *device = l->data;
                if (device == exception)
                        continue;
                if (!add_device (device_manager, device, manager, error)) {
                        ret = FALSE;
                        break;
                }
        }
        g_list_free (devices);

        return ret;
}

static GVariant *
handle_dbus_get_property (GDBusConnection  *connection,
                          const gchar      *sender,
                          const gchar      *object_path,
                          const gchar      *interface_name,
                          const gchar      *property_name,
                          GError          **error,
                          GsdCursorManager *manager)
{
        GVariant *ret;

        ret = NULL;
        if (g_strcmp0 (property_name, "ShowOSK") == 0)
                ret = g_variant_new_boolean (manager->priv->show_osk);

        return ret;
}

static void
got_session_bus (GObject          *source,
                 GAsyncResult     *res,
                 GsdCursorManager *manager)
{
        GsdCursorManagerPrivate *priv;
        GDBusConnection *connection;
        GError *error = NULL;
        const GDBusInterfaceVTable vtable = {
                NULL,
                (GDBusInterfaceGetPropertyFunc)handle_dbus_get_property,
                NULL,
        };

        connection = g_bus_get_finish (res, &error);
        if (!connection) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Couldn't get session bus: %s", error->message);
                g_error_free (error);
                return;
        }

        priv = manager->priv;
        priv->dbus_connection = connection;

        priv->dbus_register_object_id = g_dbus_connection_register_object (priv->dbus_connection,
                                                                           GSD_CURSOR_DBUS_PATH,
                                                                           priv->dbus_introspection->interfaces[0],
                                                                           &vtable,
                                                                           manager,
                                                                           NULL,
                                                                           &error);
        if (!priv->dbus_register_object_id) {
                g_warning ("Error registering object: %s", error->message);
                g_error_free (error);
                return;
        }

        priv->dbus_own_name_id = g_bus_own_name_on_connection (priv->dbus_connection,
                                                               GSD_CURSOR_DBUS_NAME,
                                                               G_BUS_NAME_OWNER_FLAGS_NONE,
                                                               NULL,
                                                               NULL,
                                                               NULL,
                                                               NULL);
}

static void
register_manager_dbus (GsdCursorManager *manager)
{
        GError *error = NULL;

        manager->priv->dbus_introspection = g_dbus_node_info_new_for_xml (introspection_xml, &error);
        if (error) {
                g_warning ("Error creating introspection data: %s", error->message);
                g_error_free (error);
                return;
        }

        g_bus_get (G_BUS_TYPE_SESSION,
                   manager->priv->cancellable,
                   (GAsyncReadyCallback) got_session_bus,
                   manager);
}

gboolean
gsd_cursor_manager_start (GsdCursorManager  *manager,
                          GError           **error)
{
        GdkDeviceManager *device_manager;

        if (gnome_settings_is_wayland ()) {
                g_debug ("Running under a wayland compositor, disabling");
                return TRUE;
        }

        g_debug ("Starting cursor manager");
        gnome_settings_profile_start (NULL);

        manager->priv->monitors = g_hash_table_new_full (g_direct_hash,
                                                         g_direct_equal,
                                                         NULL,
                                                         g_object_unref);

        if (supports_cursor_xfixes () == FALSE) {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                             "XFixes cursor extension not available");
                return FALSE;
        }

        if (supports_xinput_devices () == FALSE) {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                             "XInput support not available");
                return FALSE;
        }

        device_manager = gdk_display_get_device_manager (gdk_display_get_default ());
        manager->priv->added_id = g_signal_connect (G_OBJECT (device_manager), "device-added",
                                                    G_CALLBACK (device_added_cb), manager);
        manager->priv->removed_id = g_signal_connect (G_OBJECT (device_manager), "device-removed",
                                                      G_CALLBACK (device_removed_cb), manager);
        manager->priv->changed_id = g_signal_connect (G_OBJECT (device_manager), "device-changed",
                                                      G_CALLBACK (device_changed_cb), manager);

        if (!add_all_devices (manager, NULL, error)) {
                g_debug ("Per-device idletime monitor not available, will not hide the cursor");
                gnome_settings_profile_end (NULL);
                return FALSE;
        }

        /* Start by hiding the cursor */
        set_cursor_visibility (manager, FALSE);

        manager->priv->cancellable = g_cancellable_new ();
        register_manager_dbus (manager);

        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_cursor_manager_stop (GsdCursorManager *manager)
{
        GdkDeviceManager *device_manager;

        g_debug ("Stopping cursor manager");

        device_manager = gdk_display_get_device_manager (gdk_display_get_default ());

        if (manager->priv->added_id > 0) {
                g_signal_handler_disconnect (G_OBJECT (device_manager), manager->priv->added_id);
                manager->priv->added_id = 0;
        }

        if (manager->priv->removed_id > 0) {
                g_signal_handler_disconnect (G_OBJECT (device_manager), manager->priv->removed_id);
                manager->priv->removed_id = 0;
        }

        if (manager->priv->changed_id > 0) {
                g_signal_handler_disconnect (G_OBJECT (device_manager), manager->priv->changed_id);
                manager->priv->changed_id = 0;
        }

        if (manager->priv->cursor_shown == FALSE) {
                set_cursor_visibility (manager, TRUE);
                set_osk_enabled (manager, FALSE);
        }

        g_clear_pointer (&manager->priv->monitors, g_hash_table_destroy);

        g_cancellable_cancel (manager->priv->cancellable);
        g_clear_object (&manager->priv->cancellable);

        g_clear_pointer (&manager->priv->dbus_introspection, g_dbus_node_info_unref);
        g_clear_object (&manager->priv->dbus_connection);
}

static void
gsd_cursor_manager_finalize (GObject *object)
{
        gsd_cursor_manager_stop (GSD_CURSOR_MANAGER (object));

        G_OBJECT_CLASS (gsd_cursor_manager_parent_class)->finalize (object);
}

static void
gsd_cursor_manager_class_init (GsdCursorManagerClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_cursor_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdCursorManagerPrivate));
}

static void
gsd_cursor_manager_init (GsdCursorManager *manager)
{
        manager->priv = GSD_CURSOR_MANAGER_GET_PRIVATE (manager);
        manager->priv->cursor_shown = TRUE;

        manager->priv->show_osk = FALSE;
}

GsdCursorManager *
gsd_cursor_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_CURSOR_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_CURSOR_MANAGER (manager_object);
}
