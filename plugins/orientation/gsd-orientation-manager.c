/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2010,2011 Red Hat, Inc.
 *
 * Author: Bastien Nocera <hadess@hadess.net>
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

#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gudev/gudev.h>
#include <X11/extensions/XInput2.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-rr.h>

#include "gsd-input-helper.h"
#include "gnome-settings-profile.h"
#include "gsd-orientation-manager.h"
#include "gsd-orientation-calc.h"

#define GSD_ORIENTATION_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_ORIENTATION_MANAGER, GsdOrientationManagerPrivate))

struct GsdOrientationManagerPrivate
{
        guint start_idle_id;

        /* Accelerometer */
        char *sysfs_path;
        int device_id;

        /* Notifications */
        GUdevClient *client;
        GSettings *settings;
        gboolean orientation_lock;

        OrientationUp prev_orientation;
        int prev_x, prev_y, prev_z;

        guint orient_timeout_id;
        guint num_checks;
};

/* The maximum number of times we'll poll the X/Y/Z values
 * to check for changes */
#define MAX_CHECKS 5

#define CONF_SCHEMA "org.gnome.settings-daemon.peripherals.touchscreen"
#define ORIENTATION_LOCK_KEY "orientation-lock"

static void     gsd_orientation_manager_class_init  (GsdOrientationManagerClass *klass);
static void     gsd_orientation_manager_init        (GsdOrientationManager      *orientation_manager);
static void     gsd_orientation_manager_finalize    (GObject                    *object);

G_DEFINE_TYPE (GsdOrientationManager, gsd_orientation_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static GObject *
gsd_orientation_manager_constructor (GType                     type,
                               guint                      n_construct_properties,
                               GObjectConstructParam     *construct_properties)
{
        GsdOrientationManager      *orientation_manager;

        orientation_manager = GSD_ORIENTATION_MANAGER (G_OBJECT_CLASS (gsd_orientation_manager_parent_class)->constructor (type,
                                                                                                         n_construct_properties,
                                                                                                         construct_properties));

        return G_OBJECT (orientation_manager);
}

static void
gsd_orientation_manager_dispose (GObject *object)
{
        G_OBJECT_CLASS (gsd_orientation_manager_parent_class)->dispose (object);
}

static void
gsd_orientation_manager_class_init (GsdOrientationManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->constructor = gsd_orientation_manager_constructor;
        object_class->dispose = gsd_orientation_manager_dispose;
        object_class->finalize = gsd_orientation_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdOrientationManagerPrivate));
}

static void
gsd_orientation_manager_init (GsdOrientationManager *manager)
{
        manager->priv = GSD_ORIENTATION_MANAGER_GET_PRIVATE (manager);
        manager->priv->prev_orientation = ORIENTATION_UNDEFINED;
        manager->priv->device_id = -1;
}

static GnomeRRRotation
orientation_to_rotation (OrientationUp    orientation)
{
        switch (orientation) {
        case ORIENTATION_NORMAL:
                return GNOME_RR_ROTATION_0;
        case ORIENTATION_BOTTOM_UP:
                return GNOME_RR_ROTATION_180;
        case ORIENTATION_LEFT_UP:
                return GNOME_RR_ROTATION_90;
        case ORIENTATION_RIGHT_UP:
                return GNOME_RR_ROTATION_270;
        default:
                g_assert_not_reached ();
        }
}

static gboolean
get_current_values (GsdOrientationManager *manager,
                    int                   *x,
                    int                   *y,
                    int                   *z)
{
        int n_devices;
        XIDeviceInfo *info;
        XIValuatorClassInfo *v;

        gdk_error_trap_push ();

        info = XIQueryDevice (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), manager->priv->device_id, &n_devices);
        if (info == NULL) {
                gdk_error_trap_pop_ignored ();
                return FALSE;
        }
        gdk_error_trap_pop_ignored ();

        /* Should be XIValuatorClass type
         * as we already detected that */
        v = (XIValuatorClassInfo *) info->classes[0];
        *x = v->value;

        v = (XIValuatorClassInfo *) info->classes[1];
        *y = v->value;

        v = (XIValuatorClassInfo *) info->classes[2];
        *z = v->value;

        XIFreeDeviceInfo (info);

        return TRUE;
}

static gboolean
update_current_orientation (GsdOrientationManager *manager,
                            int x, int y, int z)
{
        OrientationUp orientation;

        g_debug ("Got values: %d, %d, %d", x, y, z);

        orientation = gsd_orientation_calc (manager->priv->prev_orientation,
                                            x, y, z);
        g_debug ("New orientation: %s (prev: %s)",
                 gsd_orientation_to_string (orientation),
                 gsd_orientation_to_string (manager->priv->prev_orientation));

        if (orientation == manager->priv->prev_orientation)
                return FALSE;

        manager->priv->prev_orientation = orientation;

        g_debug ("Orientation changed to '%s', switching screen rotation",
                 gsd_orientation_to_string (manager->priv->prev_orientation));

        return TRUE;
}

static void
do_rotation (GsdOrientationManager *manager)
{
        GnomeRRRotation rotation;

        if (manager->priv->orientation_lock) {
                g_debug ("Orientation changed, but we are locked");
                return;
        }

        rotation = orientation_to_rotation (manager->priv->prev_orientation);
        /* FIXME: call into XRandR plugin */
}

static gboolean
check_value_change_cb (GsdOrientationManager *manager)
{
        int x, y, z;

        g_debug ("checking for changed X/Y/Z, %d/%d", manager->priv->num_checks, MAX_CHECKS);

        if (get_current_values (manager, &x, &y, &z) == FALSE) {
                g_warning ("Failed to get X/Y/Z values from device '%d'", manager->priv->device_id);
                return FALSE;
        }

        if (x != manager->priv->prev_x ||
            y != manager->priv->prev_y ||
            z != manager->priv->prev_z) {
                manager->priv->num_checks = 0;

                /* We have updated values */
                if (update_current_orientation (manager, x, y, z)) {
                        do_rotation (manager);
                }

                set_device_enabled (manager->priv->device_id, FALSE);

                return FALSE;
        }

        /* If we've already checked the device MAX_CHECKS
         * times, then we don't really want to keep spinning */
        if (manager->priv->num_checks > MAX_CHECKS) {
                manager->priv->num_checks = 0;
                set_device_enabled (manager->priv->device_id, FALSE);
                return FALSE;
        }

        manager->priv->num_checks++;

        return TRUE;
}

static void
client_uevent_cb (GUdevClient           *client,
                  gchar                 *action,
                  GUdevDevice           *device,
                  GsdOrientationManager *manager)
{
        const char *sysfs_path;

        sysfs_path = g_udev_device_get_sysfs_path (device);
        g_debug ("Received uevent '%s' from '%s'", action, sysfs_path);

        if (manager->priv->orientation_lock)
                return;

        if (g_str_equal (action, "change") == FALSE)
                return;

        if (g_strcmp0 (manager->priv->sysfs_path, sysfs_path) != 0)
                return;

        g_debug ("Received an event from the accelerometer");

        if (manager->priv->orient_timeout_id > 0)
                return;

        /* Save the current value */
        if (get_current_values (manager,
                                &manager->priv->prev_x,
                                &manager->priv->prev_y,
                                &manager->priv->prev_z) == FALSE) {
                g_warning ("Failed to get current values");
                return;
        }

        if (set_device_enabled (manager->priv->device_id, TRUE) == FALSE) {
                g_warning ("Failed to re-enabled device '%d'", manager->priv->device_id);
                return;
        }

        g_timeout_add (150, (GSourceFunc) check_value_change_cb, manager);
}

static char *
get_sysfs_path (GsdOrientationManager *manager,
                const char            *device_node)
{
        GUdevDevice *device, *parent;
        char *sysfs_path;

        device = g_udev_client_query_by_device_file (manager->priv->client,
                                                     device_node);
        if (device == NULL)
                return NULL;

        parent = g_udev_device_get_parent (device);
        g_object_unref (device);
        if (parent == NULL)
                return NULL;

        sysfs_path = g_strdup (g_udev_device_get_sysfs_path (parent));
        g_object_unref (parent);

        return sysfs_path;
}

static void
orientation_lock_changed_cb (GSettings             *settings,
                             gchar                 *key,
                             GsdOrientationManager *manager)
{
        gboolean new;

        new = g_settings_get_boolean (settings, key);
        if (new == manager->priv->orientation_lock)
                return;

        manager->priv->orientation_lock = new;

        if (new == FALSE) {
                /* Handle the rotations that could have occurred while
                 * we were locked */
                do_rotation (manager);
        }
}

static gboolean
gsd_orientation_manager_idle_cb (GsdOrientationManager *manager)
{
        const char * const subsystems[] = { "input", NULL };
        char *device_node;

        gnome_settings_profile_start (NULL);

        manager->priv->settings = g_settings_new (ORIENTATION_LOCK_KEY);
        manager->priv->orientation_lock = g_settings_get_boolean (manager->priv->settings, ORIENTATION_LOCK_KEY);
        g_signal_connect (G_OBJECT (manager->priv->settings), "changed::orientation-lock",
                          G_CALLBACK (orientation_lock_changed_cb), manager);

        if (!accelerometer_is_present (&device_node,
                                       &manager->priv->device_id)) {
                g_debug ("Did not find an accelerometer");
                return FALSE;
        }
        g_debug ("Found accelerometer at '%s' (%d)",
                 device_node,
                 manager->priv->device_id);

        manager->priv->client = g_udev_client_new (subsystems);

        manager->priv->sysfs_path = get_sysfs_path (manager, device_node);

        if (manager->priv->sysfs_path == NULL) {
                g_debug ("Could not find sysfs path for '%s'", device_node);
                g_free (device_node);
                return FALSE;
        }
        g_debug ("Found accelerometer at sysfs path '%s'", manager->priv->sysfs_path);
        g_free (device_node);

        set_device_enabled (manager->priv->device_id, FALSE);

        g_signal_connect (G_OBJECT (manager->priv->client), "uevent",
                          G_CALLBACK (client_uevent_cb), manager);

        return FALSE;
}

gboolean
gsd_orientation_manager_start (GsdOrientationManager *manager,
                         GError         **error)
{
        gnome_settings_profile_start (NULL);

        manager->priv->start_idle_id = g_idle_add ((GSourceFunc) gsd_orientation_manager_idle_cb, manager);

        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_orientation_manager_stop (GsdOrientationManager *manager)
{
        GsdOrientationManagerPrivate *p = manager->priv;

        g_debug ("Stopping orientation manager");

        if (p->orient_timeout_id > 0) {
                g_source_remove (p->orient_timeout_id);
                p->orient_timeout_id = 0;
        }

        if (p->settings) {
                g_object_unref (p->settings);
                p->settings = NULL;
        }

        if (p->sysfs_path) {
                g_free (p->sysfs_path);
                p->sysfs_path = NULL;
        }

        if (p->device_id > 0) {
                set_device_enabled (p->device_id, TRUE);
                p->device_id = -1;
        }

        if (p->client) {
                g_object_unref (p->client);
                p->client = NULL;
        }
}

static void
gsd_orientation_manager_finalize (GObject *object)
{
        GsdOrientationManager *orientation_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_ORIENTATION_MANAGER (object));

        orientation_manager = GSD_ORIENTATION_MANAGER (object);

        g_return_if_fail (orientation_manager->priv != NULL);

        if (orientation_manager->priv->start_idle_id != 0)
                g_source_remove (orientation_manager->priv->start_idle_id);

        G_OBJECT_CLASS (gsd_orientation_manager_parent_class)->finalize (object);
}

GsdOrientationManager *
gsd_orientation_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_ORIENTATION_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_ORIENTATION_MANAGER (manager_object);
}
