/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright 2002 Sun Microsystems, Inc.
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
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

/*
 * WARNING: This is a hack.
 *
 * All it does is keep the "text / *" and "text/plain" mime type
 * handlers in sync with each other.  The reason we do this is because
 * there is no UI for editing the text / * handler, and this is probably
 * what the user actually wants to do.
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
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>

#include <libgnomevfs/gnome-vfs-mime-handlers.h>
#include <libgnomevfs/gnome-vfs-mime-monitor.h>
#include <libgnomevfs/gnome-vfs.h>

#include "gsd-default-editor-manager.h"

#define GSD_DEFAULT_EDITOR_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_DEFAULT_EDITOR_MANAGER, GsdDefaultEditorManagerPrivate))

#define SYNC_CHANGES_KEY "/apps/gnome_settings_daemon/default_editor/sync_text_types"

struct GsdDefaultEditorManagerPrivate
{
        gboolean sync_changes;
};

static void     gsd_default_editor_manager_class_init  (GsdDefaultEditorManagerClass *klass);
static void     gsd_default_editor_manager_init        (GsdDefaultEditorManager      *default_editor_manager);
static void     gsd_default_editor_manager_finalize    (GObject             *object);

G_DEFINE_TYPE (GsdDefaultEditorManager, gsd_default_editor_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static void
sync_changes_cb (GConfClient             *client,
                 guint                    cnxn_id,
                 GConfEntry              *entry,
                 GsdDefaultEditorManager *manager)
{
        GConfValue *value;

        value = gconf_entry_get_value (entry);
        manager->priv->sync_changes = gconf_value_get_bool (value);
}

static void
register_config_callback (GsdDefaultEditorManager *manager,
                          const char              *path,
                          GConfClientNotifyFunc    func)
{
        GConfClient *client;

        client = gconf_client_get_default ();

        gconf_client_add_dir (client, path, GCONF_CLIENT_PRELOAD_NONE, NULL);
        gconf_client_notify_add (client, path, func, manager, NULL, NULL);

        g_object_unref (client);
}

static void
vfs_change_cb (GnomeVFSMIMEMonitor     *monitor,
               GsdDefaultEditorManager *manager)
{
        GnomeVFSMimeApplication *star_app;
        GnomeVFSMimeApplication *plain_app;
        GnomeVFSMimeActionType   action;

        if (!manager->priv->sync_changes) {
                return;
        }

        star_app  = gnome_vfs_mime_get_default_application ("text/*");
        plain_app = gnome_vfs_mime_get_default_application ("text/plain");

        if (star_app == NULL || plain_app == NULL) {
                if (star_app != NULL) {
                    gnome_vfs_mime_application_free (star_app);
                }
                if (plain_app != NULL) {
                    gnome_vfs_mime_application_free (plain_app);
                }
                return;
        }
        if (!strcmp (star_app->id, plain_app->id)) {
                gnome_vfs_mime_application_free (star_app);
                gnome_vfs_mime_application_free (plain_app);
                return;
        }

#ifdef DE_DEBUG
        g_message ("Synching text/plain to text/*...");
#endif

        action = gnome_vfs_mime_get_default_action_type ("text/plain");

        gnome_vfs_mime_set_default_application ("text/*", plain_app->id);
        gnome_vfs_mime_application_free (plain_app);

        gnome_vfs_mime_set_default_action_type ("text/*", action);
}

gboolean
gsd_default_editor_manager_start (GsdDefaultEditorManager *manager,
                                  GError                 **error)
{
        GConfClient *client;

        g_debug ("Starting default_editor manager");

        client = gconf_client_get_default ();

        manager->priv->sync_changes = gconf_client_get_bool (client, SYNC_CHANGES_KEY, NULL);

        g_object_unref (client);

        register_config_callback (manager, SYNC_CHANGES_KEY, (GConfClientNotifyFunc) sync_changes_cb);

        g_signal_connect (gnome_vfs_mime_monitor_get (),
                          "data_changed",
                          G_CALLBACK (vfs_change_cb),
                          manager);

        vfs_change_cb (NULL, manager);

        return TRUE;
}

void
gsd_default_editor_manager_stop (GsdDefaultEditorManager *manager)
{
        g_debug ("Stopping default_editor manager");
}

static void
gsd_default_editor_manager_set_property (GObject        *object,
                               guint           prop_id,
                               const GValue   *value,
                               GParamSpec     *pspec)
{
        GsdDefaultEditorManager *self;

        self = GSD_DEFAULT_EDITOR_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsd_default_editor_manager_get_property (GObject        *object,
                               guint           prop_id,
                               GValue         *value,
                               GParamSpec     *pspec)
{
        GsdDefaultEditorManager *self;

        self = GSD_DEFAULT_EDITOR_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gsd_default_editor_manager_constructor (GType                  type,
                              guint                  n_construct_properties,
                              GObjectConstructParam *construct_properties)
{
        GsdDefaultEditorManager      *default_editor_manager;
        GsdDefaultEditorManagerClass *klass;

        klass = GSD_DEFAULT_EDITOR_MANAGER_CLASS (g_type_class_peek (GSD_TYPE_DEFAULT_EDITOR_MANAGER));

        default_editor_manager = GSD_DEFAULT_EDITOR_MANAGER (G_OBJECT_CLASS (gsd_default_editor_manager_parent_class)->constructor (type,
                                                                                                      n_construct_properties,
                                                                                                      construct_properties));

        return G_OBJECT (default_editor_manager);
}

static void
gsd_default_editor_manager_dispose (GObject *object)
{
        GsdDefaultEditorManager *default_editor_manager;

        default_editor_manager = GSD_DEFAULT_EDITOR_MANAGER (object);

        G_OBJECT_CLASS (gsd_default_editor_manager_parent_class)->dispose (object);
}

static void
gsd_default_editor_manager_class_init (GsdDefaultEditorManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsd_default_editor_manager_get_property;
        object_class->set_property = gsd_default_editor_manager_set_property;
        object_class->constructor = gsd_default_editor_manager_constructor;
        object_class->dispose = gsd_default_editor_manager_dispose;
        object_class->finalize = gsd_default_editor_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdDefaultEditorManagerPrivate));
}

static void
gsd_default_editor_manager_init (GsdDefaultEditorManager *manager)
{
        manager->priv = GSD_DEFAULT_EDITOR_MANAGER_GET_PRIVATE (manager);

        gnome_vfs_init ();
}

static void
gsd_default_editor_manager_finalize (GObject *object)
{
        GsdDefaultEditorManager *default_editor_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_DEFAULT_EDITOR_MANAGER (object));

        default_editor_manager = GSD_DEFAULT_EDITOR_MANAGER (object);

        g_return_if_fail (default_editor_manager->priv != NULL);

        G_OBJECT_CLASS (gsd_default_editor_manager_parent_class)->finalize (object);
}

GsdDefaultEditorManager *
gsd_default_editor_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_DEFAULT_EDITOR_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_DEFAULT_EDITOR_MANAGER (manager_object);
}
