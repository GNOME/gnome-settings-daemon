/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Red Hat, Inc.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Tomas Bzatek <tbzatek@redhat.com>
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include "gnome-settings-profile.h"
#include "gsd-automount-manager.h"
#include "nautilus-autorun.h"

#define GSD_AUTOMOUNT_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_AUTOMOUNT_MANAGER, GsdAutomountManagerPrivate))

struct GsdAutomountManagerPrivate
{
        GSettings   *settings;

	GVolumeMonitor *volume_monitor;
	unsigned int automount_idle_id;
};

static void     gsd_automount_manager_class_init  (GsdAutomountManagerClass *klass);
static void     gsd_automount_manager_init        (GsdAutomountManager      *gsd_automount_manager);
static void     gsd_automount_manager_finalize    (GObject             *object);

G_DEFINE_TYPE (GsdAutomountManager, gsd_automount_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;


static void
startup_volume_mount_cb (GObject *source_object,
			 GAsyncResult *res,
			 gpointer user_data)
{
	g_volume_mount_finish (G_VOLUME (source_object), res, NULL);
}

static void
automount_all_volumes (GsdAutomountManager *manager)
{
	GList *volumes, *l;
	GMount *mount;
	GVolume *volume;

	if (g_settings_get_boolean (manager->priv->settings, "automount")) {
		/* automount all mountable volumes at start-up */
		volumes = g_volume_monitor_get_volumes (manager->priv->volume_monitor);
		for (l = volumes; l != NULL; l = l->next) {
			volume = l->data;

			if (!g_volume_should_automount (volume) ||
			    !g_volume_can_mount (volume)) {
				continue;
			}

			mount = g_volume_get_mount (volume);
			if (mount != NULL) {
				g_object_unref (mount);
				continue;
			}

			/* pass NULL as GMountOperation to avoid user interaction */
			g_volume_mount (volume, 0, NULL, NULL, startup_volume_mount_cb, NULL);
		}
		g_list_foreach (volumes, (GFunc) g_object_unref, NULL);
		g_list_free (volumes);
	}
}

static gboolean
automount_all_volumes_idle_cb (gpointer data)
{
	GsdAutomountManager *manager = GSD_AUTOMOUNT_MANAGER (data);

	automount_all_volumes (manager);

	manager->priv->automount_idle_id = 0;
	return FALSE;
}

static void
volume_mount_cb (GObject *source_object,
		 GAsyncResult *res,
		 gpointer user_data)
{
	GMountOperation *mount_op = user_data;
	GError *error;
	char *primary;
	char *name;

	error = NULL;
	nautilus_allow_autorun_for_volume_finish (G_VOLUME (source_object));
	if (!g_volume_mount_finish (G_VOLUME (source_object), res, &error)) {
		if (error->code != G_IO_ERROR_FAILED_HANDLED) {
			name = g_volume_get_name (G_VOLUME (source_object));
			primary = g_strdup_printf (_("Unable to mount %s"), name);
			g_free (name);
			show_error_dialog (primary,
				           error->message,
					   NULL);
			g_free (primary);
		}
		g_error_free (error);
	}

	g_object_unref (mount_op);
}

static void
nautilus_file_operations_mount_volume (GtkWindow *parent_window,
				       GVolume *volume,
				       gboolean allow_autorun)
{
	GMountOperation *mount_op;

	mount_op = gtk_mount_operation_new (parent_window);
	g_mount_operation_set_password_save (mount_op, G_PASSWORD_SAVE_FOR_SESSION);

	if (allow_autorun) {
		nautilus_allow_autorun_for_volume (volume);
	}
	g_volume_mount (volume, 0, mount_op, NULL, volume_mount_cb, mount_op);
}

static void
volume_added_callback (GVolumeMonitor *monitor,
		       GVolume *volume,
		       GsdAutomountManager *manager)
{
	if (g_settings_get_boolean (manager->priv->settings, "automount") &&
	    g_volume_should_automount (volume) &&
	    g_volume_can_mount (volume)) {
		nautilus_file_operations_mount_volume (NULL, volume, TRUE);
	} else {
		/* Allow nautilus_autorun() to run. When the mount is later
		 * added programmatically (i.e. for a blank CD),
		 * nautilus_autorun() will be called by mount_added_callback(). */
		nautilus_allow_autorun_for_volume (volume);
		nautilus_allow_autorun_for_volume_finish (volume);
	}
}

static void
autorun_show_window (GMount *mount, gpointer user_data)
{
	GFile *location;
        char *uri;
        GError *error;
	char *primary;
	char *name;

	location = g_mount_get_root (mount);
        uri = g_file_get_uri (location);

        error = NULL;
	/* use default folder handler */
        if (! gtk_show_uri (NULL, uri, GDK_CURRENT_TIME, &error)) {
		name = g_mount_get_name (mount);
		primary = g_strdup_printf (_("Unable to open a folder for %s"), name);
		g_free (name);
        	show_error_dialog (primary,
        		           error->message,
        			   NULL);
		g_free (primary);
        	g_error_free (error);
        }

        g_free (uri);
	g_object_unref (location);
}

static void
mount_added_callback (GVolumeMonitor *monitor,
		      GMount *mount,
		      GsdAutomountManager *manager)
{
	nautilus_autorun (mount, manager->priv->settings, autorun_show_window, manager);
}

static void
setup_automounter (GsdAutomountManager *manager)
{
	manager->priv->volume_monitor = g_volume_monitor_get ();
	g_signal_connect_object (manager->priv->volume_monitor, "mount_added",
				 G_CALLBACK (mount_added_callback), manager, 0);
	g_signal_connect_object (manager->priv->volume_monitor, "volume_added",
				 G_CALLBACK (volume_added_callback), manager, 0);

	manager->priv->automount_idle_id =
		g_idle_add_full (G_PRIORITY_LOW,
				 automount_all_volumes_idle_cb,
				 manager, NULL);
}

gboolean
gsd_automount_manager_start (GsdAutomountManager *manager,
                                       GError              **error)
{
        g_debug ("Starting automounting manager");
        gnome_settings_profile_start (NULL);

        manager->priv->settings = g_settings_new ("org.gnome.media-handling");
        setup_automounter (manager);

        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_automount_manager_stop (GsdAutomountManager *manager)
{
        GsdAutomountManagerPrivate *p = manager->priv;

        g_debug ("Stopping automounting manager");

	if (p->volume_monitor) {
		g_object_unref (p->volume_monitor);
		p->volume_monitor = NULL;
	}

	if (p->automount_idle_id != 0) {
		g_source_remove (p->automount_idle_id);
		p->automount_idle_id = 0;
	}

        if (p->settings != NULL) {
                g_object_unref (p->settings);
                p->settings = NULL;
        }
}

static void
gsd_automount_manager_set_property (GObject        *object,
                                     guint           prop_id,
                                     const GValue   *value,
                                     GParamSpec     *pspec)
{
        GsdAutomountManager *self;

        self = GSD_AUTOMOUNT_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsd_automount_manager_get_property (GObject        *object,
                                     guint           prop_id,
                                     GValue         *value,
                                     GParamSpec     *pspec)
{
        GsdAutomountManager *self;

        self = GSD_AUTOMOUNT_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gsd_automount_manager_constructor (GType                  type,
                                    guint                  n_construct_properties,
                                    GObjectConstructParam *construct_properties)
{
        GsdAutomountManager      *gsd_automount_manager;
        GsdAutomountManagerClass *klass;

        klass = GSD_AUTOMOUNT_MANAGER_CLASS (g_type_class_peek (GSD_TYPE_AUTOMOUNT_MANAGER));

        gsd_automount_manager = GSD_AUTOMOUNT_MANAGER (G_OBJECT_CLASS (gsd_automount_manager_parent_class)->constructor (type,
                                                                                                                        n_construct_properties,
                                                                                                                        construct_properties));

        return G_OBJECT (gsd_automount_manager);
}

static void
gsd_automount_manager_dispose (GObject *object)
{
        GsdAutomountManager *gsd_automount_manager;

        gsd_automount_manager = GSD_AUTOMOUNT_MANAGER (object);

        G_OBJECT_CLASS (gsd_automount_manager_parent_class)->dispose (object);
}

static void
gsd_automount_manager_class_init (GsdAutomountManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsd_automount_manager_get_property;
        object_class->set_property = gsd_automount_manager_set_property;
        object_class->constructor = gsd_automount_manager_constructor;
        object_class->dispose = gsd_automount_manager_dispose;
        object_class->finalize = gsd_automount_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdAutomountManagerPrivate));
}

static void
gsd_automount_manager_init (GsdAutomountManager *manager)
{
        manager->priv = GSD_AUTOMOUNT_MANAGER_GET_PRIVATE (manager);
}

static void
gsd_automount_manager_finalize (GObject *object)
{
        GsdAutomountManager *gsd_automount_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_AUTOMOUNT_MANAGER (object));

        gsd_automount_manager = GSD_AUTOMOUNT_MANAGER (object);

        g_return_if_fail (gsd_automount_manager->priv != NULL);

        G_OBJECT_CLASS (gsd_automount_manager_parent_class)->finalize (object);
}

GsdAutomountManager *
gsd_automount_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_AUTOMOUNT_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_AUTOMOUNT_MANAGER (manager_object);
}
