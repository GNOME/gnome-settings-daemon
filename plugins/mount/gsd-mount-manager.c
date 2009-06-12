/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "gsd-mount-manager.h"

#define GSD_MOUNT_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_MOUNT_MANAGER, GsdMountManagerPrivate))

struct GsdMountManagerPrivate
{
        GVolumeMonitor *monitor;
};

G_DEFINE_TYPE (GsdMountManager, gsd_mount_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

#if 0
static void
drive_connected_cb (GVolumeMonitor *monitor,
                    GDrive *drive,
                    GsdMountManager *manager)
{
        /* TODO: listen for the eject button */
}
#endif

static void
volume_mounted_cb (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
        GError *error = NULL;

	if (!g_volume_mount_finish (G_VOLUME (source_object), result, &error)) {
		if (error->code != G_IO_ERROR_FAILED_HANDLED) {
                        char *name, *primary;
                        GtkWidget *dialog;

			name = g_volume_get_name (G_VOLUME (source_object));
			primary = g_strdup_printf (_("Unable to mount %s"), name);
			g_free (name);

                        dialog = gtk_message_dialog_new (NULL, 0,
                                                         GTK_MESSAGE_ERROR,
                                                         GTK_BUTTONS_CLOSE,
                                                         primary);

			g_free (primary);
                        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), error->message);

                        gtk_dialog_run (GTK_DIALOG (dialog));
                        gtk_widget_destroy (dialog);
		}
		g_error_free (error);
	}

}

static void
volume_added_cb (GVolumeMonitor *monitor,
                 GVolume *volume,
                 GsdMountManager *manager)
{
        if (g_volume_can_mount (volume)) {
                GMountOperation *mount_op;

                mount_op = gtk_mount_operation_new (NULL);
                g_volume_mount (volume, G_MOUNT_MOUNT_NONE,
                                mount_op, NULL,
                                volume_mounted_cb, manager);
        }
}

static void
mount_added_cb (GVolumeMonitor *monitor,
                GMount *mount,
                GsdMountManager *manager)
{
        GFile *file;
        char *uri;

        file = g_mount_get_root (mount);
        uri = g_file_get_uri (file);

        /* TODO: error */
        gtk_show_uri (NULL, uri, GDK_CURRENT_TIME, NULL);

        g_free (uri);
        g_object_unref (file);
}

gboolean
gsd_mount_manager_start (GsdMountManager *manager,
                               GError               **error)
{
        g_debug ("Starting mount manager");

        manager->priv->monitor = g_volume_monitor_get ();

#if 0
	g_signal_connect_object (manager->priv->monitor, "drive-connected",
				 G_CALLBACK (drive_connected_cb), manager, 0);
#endif
	g_signal_connect_object (manager->priv->monitor, "volume-added",
				 G_CALLBACK (volume_added_cb), manager, 0);
	g_signal_connect_object (manager->priv->monitor, "mount-added",
				 G_CALLBACK (mount_added_cb), manager, 0);

        /* TODO: handle eject buttons */
        return TRUE;
}

void
gsd_mount_manager_stop (GsdMountManager *manager)
{
        g_debug ("Stopping mount manager");
}

static void
gsd_mount_manager_dispose (GObject *object)
{
        GsdMountManager *manager = GSD_MOUNT_MANAGER (object);

        if (manager->priv->monitor) {
                g_signal_handlers_disconnect_by_func
                        (manager->priv->monitor, volume_added_cb, manager);
                g_signal_handlers_disconnect_by_func
                        (manager->priv->monitor, mount_added_cb, manager);
                g_object_unref (manager->priv->monitor);
                manager->priv->monitor = NULL;
        }

        G_OBJECT_CLASS (gsd_mount_manager_parent_class)->dispose (object);
}

static void
gsd_mount_manager_init (GsdMountManager *manager)
{
        manager->priv = GSD_MOUNT_MANAGER_GET_PRIVATE (manager);
}

static void
gsd_mount_manager_finalize (GObject *object)
{
        GsdMountManager *mount_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_MOUNT_MANAGER (object));

        mount_manager = GSD_MOUNT_MANAGER (object);

        g_return_if_fail (mount_manager->priv != NULL);

        G_OBJECT_CLASS (gsd_mount_manager_parent_class)->finalize (object);
}

static void
gsd_mount_manager_class_init (GsdMountManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->dispose = gsd_mount_manager_dispose;
        object_class->finalize = gsd_mount_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdMountManagerPrivate));
}

GsdMountManager *
gsd_mount_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_MOUNT_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_MOUNT_MANAGER (manager_object);
}
