/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Rodrigo Moya <rodrigo@gnome.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
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
#include "conf-watcher.h"
#include "gsd-gconf-manager.h"

#define GSD_GCONF_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_GCONF_MANAGER, GsdGconfManagerPrivate))

struct GsdGconfManagerPrivate {
        GHashTable *conf_watchers;
};

GsdGconfManager *manager_object = NULL;

G_DEFINE_TYPE(GsdGconfManager, gsd_gconf_manager, G_TYPE_OBJECT)

static void
gsd_gconf_manager_finalize (GObject *object)
{
        GsdGconfManager *manager = GSD_GCONF_MANAGER (object);

        g_return_if_fail (manager->priv != NULL);

        if (manager->priv->conf_watchers != NULL)
                g_hash_table_destroy (manager->priv->conf_watchers);

        G_OBJECT_CLASS (gsd_gconf_manager_parent_class)->finalize (object);
}

static void
gsd_gconf_manager_class_init (GsdGconfManagerClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_gconf_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdGconfManagerPrivate));
}

static void
gsd_gconf_manager_init (GsdGconfManager *manager)
{
        manager->priv = GSD_GCONF_MANAGER_GET_PRIVATE (manager);
}

GsdGconfManager *
gsd_gconf_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_GCONF_MANAGER, NULL);
                g_object_add_weak_pointer ((gpointer) manager_object,
                                           (gpointer *) &manager_object);
        }

        return manager_object;
}

gboolean
gsd_gconf_manager_start (GsdGconfManager *manager, GError **error)
{
        GDir *convertdir;
        gboolean result = FALSE;

        manager->priv->conf_watchers = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

        /* Read all conversion files from GCONF_SETTINGS_CONVERTDIR */
        convertdir = g_dir_open (GCONF_SETTINGS_CONVERTDIR, 0, error);
        if (convertdir) {
                const gchar *filename;

                while ((filename = g_dir_read_name (convertdir))) {
                        gchar *path, **groups;
                        gsize group_len, i;
                        GKeyFile *key_file = g_key_file_new ();

                        path = g_build_filename (GCONF_SETTINGS_CONVERTDIR, filename, NULL);
                        if (!g_key_file_load_from_file (key_file, path, G_KEY_FILE_NONE, error)) {
                                g_free (path);
                                g_key_file_free (key_file);
                                result = FALSE;
                                break;
                        }

                        /* Load the groups in the file */
                        groups = g_key_file_get_groups (key_file, &group_len);
                        for (i = 0; i < group_len; i++) {
                                gchar **keys;
                                gsize key_len, j;
                                GHashTable *keys_hash = NULL;

                                if (!groups[i])
                                        continue;

                                keys = g_key_file_get_keys (key_file, groups[i], &key_len, error);
                                for (j = 0; j < key_len; j++) {
                                        if (keys_hash == NULL)
                                                keys_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

                                        g_hash_table_insert (keys_hash, g_strdup (keys[j]),
                                                             g_key_file_get_value (key_file, groups[i], keys[j], error));
                                }

                                g_strfreev (keys);

                                if (keys_hash != NULL) {
                                        ConfWatcher *watcher;

                                        watcher = conf_watcher_new (groups[i], keys_hash);
                                        if (watcher) {
                                                g_hash_table_insert (manager->priv->conf_watchers,
                                                                     g_strdup (groups[i]),
                                                                     watcher);
                                        } else
                                                g_hash_table_destroy (keys_hash);
                                }
                        }

                        /* Free all memory */
                        g_free (path);
                        g_strfreev (groups);
                        g_key_file_free (key_file);

                        result = TRUE;
                }

                g_dir_close (convertdir);
        }

        return result;
}

void
gsd_gconf_manager_stop (GsdGconfManager *manager)
{
        if (manager->priv->conf_watchers != NULL) {
                g_hash_table_destroy (manager->priv->conf_watchers);
                manager->priv->conf_watchers = NULL;
        }
}
