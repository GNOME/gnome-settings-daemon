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

#include "gsd-input-helper.h"
#include "gnome-settings-profile.h"
#include "gsd-orientation-manager.h"
#include "gsd-orientation-calc.h"

#define GSD_ORIENTATION_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_ORIENTATION_MANAGER, GsdOrientationManagerPrivate))

struct GsdOrientationManagerPrivate
{
        guint start_idle_id;
        char *device_node;
        int device_id;
        OrientationUp prev_orientation;
        GUdevClient *client;
};

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
update_current_orientation (GsdOrientationManager *manager)
{
        OrientationUp orientation;
        int x, y, z;

        if (get_current_values (manager, &x, &y, &z) == FALSE) {
                g_warning ("Failed to get X/Y/Z values from device '%d'", manager->priv->device_id);
                return FALSE;
        }
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
client_uevent_cb (GUdevClient           *client,
                  gchar                 *action,
                  GUdevDevice           *device,
                  GsdOrientationManager *manager)
{
        const char *device_node;

        device_node = g_udev_device_get_device_file (device);
        g_debug ("Received uevent '%s' from '%s'", action, device_node);

        if (g_str_equal (action, "change") == FALSE)
                return;

        if (g_strcmp0 (manager->priv->device_node, device_node) != 0)
                return;

        g_debug ("Received an event from the accelerometer");

        if (set_device_enabled (manager->priv->device_id, TRUE) == FALSE) {
                g_warning ("Failed to re-enabled device '%d'", manager->priv->device_id);
                return;
        }

        if (update_current_orientation (manager)) {
                /* FIXME: call into XRandR plugin */
        }

        set_device_enabled (manager->priv->device_id, FALSE);
}

static gboolean
gsd_orientation_manager_idle_cb (GsdOrientationManager *manager)
{
        const char * const subsystems[] = { "input", NULL };

        gnome_settings_profile_start (NULL);

        if (!accelerometer_is_present (&manager->priv->device_node,
                                       &manager->priv->device_id)) {
                g_debug ("Did not find an accelerometer");
                return FALSE;
        }
        g_debug ("Found accelerometer at '%s' (%d)",
                 manager->priv->device_node,
                 manager->priv->device_id);

        update_current_orientation (manager);

        set_device_enabled (manager->priv->device_id, FALSE);

        manager->priv->client = g_udev_client_new (subsystems);
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

        if (p->device_node) {
                g_free (p->device_node);
                p->device_node = NULL;
        }

        p->device_id = -1;

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
