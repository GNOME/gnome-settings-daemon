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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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

#include "gnome-settings-profile.h"
#include "gsd-cursor-manager.h"
#include "gsd-input-helper.h"

#define XFIXES_CURSOR_HIDING_MAJOR 4

#define GSD_CURSOR_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_CURSOR_MANAGER, GsdCursorManagerPrivate))

struct GsdCursorManagerPrivate
{
        guint added_id;
        guint removed_id;
        guint changed_id;
        gboolean cursor_shown;
        GHashTable *monitors;
};

static void     gsd_cursor_manager_class_init  (GsdCursorManagerClass *klass);
static void     gsd_cursor_manager_init        (GsdCursorManager      *cursor_manager);
static void     gsd_cursor_manager_finalize    (GObject               *object);

G_DEFINE_TYPE (GsdCursorManager, gsd_cursor_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static gboolean add_all_devices (GsdCursorManager *manager, GdkDevice *exception, GError **error);

typedef void (*ForeachScreenFunc) (GdkDisplay *display, GdkScreen *screen, GsdCursorManager *manager, gpointer user_data);

static void
foreach_screen (GsdCursorManager  *manager,
                ForeachScreenFunc  func,
                gpointer           user_data)
{
        GdkDisplay *display;
        guint n_screens;
        guint i;

        display = gdk_display_get_default ();
        n_screens = gdk_display_get_n_screens (display);
        for (i = 0; i < n_screens; i++) {
                GdkScreen *screen;

                screen = gdk_display_get_screen (display, i);
                (func) (display, screen, manager, user_data);
        }
}

static void
set_cursor_visibility_foreach (GdkDisplay       *display,
                               GdkScreen        *screen,
                               GsdCursorManager *manager,
                               gpointer          user_data)
{
        Display *xdisplay;
        gboolean visible = GPOINTER_TO_INT (user_data);

        xdisplay = GDK_DISPLAY_XDISPLAY (display);

        if (visible)
                XFixesShowCursor (xdisplay, GDK_WINDOW_XID (gdk_screen_get_root_window (screen)));
        else
                XFixesHideCursor (xdisplay, GDK_WINDOW_XID (gdk_screen_get_root_window (screen)));
}

static void
set_cursor_visibility (GsdCursorManager *manager,
                       gboolean          visible)
{
        g_debug ("Attempting to %s the cursor", visible ? "show" : "hide");

        if (manager->priv->cursor_shown == visible)
                return;

        gdk_error_trap_push ();
        foreach_screen (manager, set_cursor_visibility_foreach, GINT_TO_POINTER (visible));
        if (gdk_error_trap_pop ()) {
                g_warning ("An error occurred trying to %s the cursor",
                           visible ? "show" : "hide");
        }

        manager->priv->cursor_shown = visible;
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
        monitor = gnome_idle_monitor_new_for_device (device);
        if (!monitor) {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                             "Per-device idletime monitor not available");
                return FALSE;
        }
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

gboolean
gsd_cursor_manager_start (GsdCursorManager  *manager,
                          GError           **error)
{
        GdkDeviceManager *device_manager;

        g_debug ("Starting cursor manager");
        gnome_settings_profile_start (NULL);

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

        /* Start by hiding the cursor, and then initialising the default
         * root window cursor, as the window manager shouldn't do that. */
        set_cursor_visibility (manager, FALSE);

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

        if (manager->priv->cursor_shown == FALSE)
                set_cursor_visibility (manager, TRUE);
}

static void
gsd_cursor_manager_class_init (GsdCursorManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_cursor_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdCursorManagerPrivate));
}

static void
gsd_cursor_manager_init (GsdCursorManager *manager)
{
        manager->priv = GSD_CURSOR_MANAGER_GET_PRIVATE (manager);
        manager->priv->cursor_shown = TRUE;
        manager->priv->monitors = g_hash_table_new_full (g_direct_hash,
                                                         g_direct_equal,
                                                         NULL,
                                                         g_object_unref);
}

static void
gsd_cursor_manager_finalize (GObject *object)
{
        GsdCursorManager *cursor_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_CURSOR_MANAGER (object));

        cursor_manager = GSD_CURSOR_MANAGER (object);

        g_clear_pointer (&cursor_manager->priv->monitors, g_hash_table_destroy);

        G_OBJECT_CLASS (gsd_cursor_manager_parent_class)->finalize (object);
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
