/*
 * Copyright (C) 2008 Michael J. Chudobiak <mjc@avtechpulse.com>
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

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

#include "gnome-settings-profile.h"
#include "gsd-housekeeping-manager.h"
#include "gsd-disk-space.h"


/* General */
#define INTERVAL_ONCE_A_DAY 24*60*60
#define INTERVAL_TWO_MINUTES 2*60

/* Thumbnail cleaner */
#define THUMB_PREFIX "org.gnome.desktop.thumbnail-cache"

#define THUMB_AGE_KEY "maximum-age"
#define THUMB_SIZE_KEY "maximum-size"

#define GSD_HOUSEKEEPING_DBUS_PATH "/org/gnome/SettingsDaemon/Housekeeping"

static const gchar introspection_xml[] =
"<node>"
"  <interface name='org.gnome.SettingsDaemon.Housekeeping'>"
"    <method name='EmptyTrash'/>"
"    <method name='RemoveTempFiles'/>"
"  </interface>"
"</node>";

struct GsdHousekeepingManagerPrivate {
        GSettings *settings;
        guint long_term_cb;
        guint short_term_cb;

        GDBusNodeInfo   *introspection_data;
        GDBusConnection *connection;
        GCancellable    *bus_cancellable;
};

#define GSD_HOUSEKEEPING_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_HOUSEKEEPING_MANAGER, GsdHousekeepingManagerPrivate))

static void     gsd_housekeeping_manager_class_init  (GsdHousekeepingManagerClass *klass);
static void     gsd_housekeeping_manager_init        (GsdHousekeepingManager      *housekeeping_manager);

G_DEFINE_TYPE (GsdHousekeepingManager, gsd_housekeeping_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;


typedef struct {
        glong now;
        glong max_age;
        goffset total_size;
        goffset max_size;
} PurgeData;


typedef struct {
        time_t  mtime;
        char   *path;
        glong   size;
} ThumbData;


static void
thumb_data_free (gpointer data)
{
        ThumbData *info = data;

        if (info) {
                g_free (info->path);
                g_free (info);
        }
}

static GList *
read_dir_for_purge (const char *path, GList *files)
{
        GFile           *read_path;
        GFileEnumerator *enum_dir;

        read_path = g_file_new_for_path (path);
        enum_dir = g_file_enumerate_children (read_path,
                                              G_FILE_ATTRIBUTE_STANDARD_NAME ","
                                              G_FILE_ATTRIBUTE_TIME_MODIFIED ","
                                              G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                              G_FILE_QUERY_INFO_NONE,
                                              NULL,
                                              NULL);

        if (enum_dir != NULL) {
                GFileInfo *info;
                while ((info = g_file_enumerator_next_file (enum_dir, NULL, NULL)) != NULL) {
                        const char *name;
                        name = g_file_info_get_name (info);

                        if (strlen (name) == 36 && strcmp (name + 32, ".png") == 0) {
                                ThumbData *td;
                                GFile     *entry;
                                char      *entry_path;
                                GTimeVal   mod_time;

                                entry = g_file_get_child (read_path, name);
                                entry_path = g_file_get_path (entry);
                                g_object_unref (entry);

                                g_file_info_get_modification_time (info, &mod_time);

                                td = g_new0 (ThumbData, 1);
                                td->path = entry_path;
                                td->mtime = mod_time.tv_sec;
                                td->size = g_file_info_get_size (info);

                                files = g_list_prepend (files, td);
                        }
                        g_object_unref (info);
                }
                g_object_unref (enum_dir);
        }
        g_object_unref (read_path);

        return files;
}

static void
purge_old_thumbnails (ThumbData *info, PurgeData *purge_data)
{
        if ((purge_data->now - info->mtime) > purge_data->max_age) {
                g_unlink (info->path);
                info->size = 0;
        } else {
                purge_data->total_size += info->size;
        }
}

static int
sort_file_mtime (ThumbData *file1, ThumbData *file2)
{
        return file1->mtime - file2->mtime;
}

static char **
get_thumbnail_dirs (void)
{
        GPtrArray *array;
        char *path;

        array = g_ptr_array_new ();

        /* check new XDG cache */
        path = g_build_filename (g_get_user_cache_dir (),
                                 "thumbnails",
                                 "normal",
                                 NULL);
        g_ptr_array_add (array, path);

        path = g_build_filename (g_get_user_cache_dir (),
                                 "thumbnails",
                                 "large",
                                 NULL);
        g_ptr_array_add (array, path);

        path = g_build_filename (g_get_user_cache_dir (),
                                 "thumbnails",
                                 "fail",
                                 "gnome-thumbnail-factory",
                                 NULL);
        g_ptr_array_add (array, path);

        /* cleanup obsolete locations too */
        path = g_build_filename (g_get_home_dir (),
                                 ".thumbnails",
                                 "normal",
                                 NULL);
        g_ptr_array_add (array, path);

        path = g_build_filename (g_get_home_dir (),
                                 ".thumbnails",
                                 "large",
                                 NULL);
        g_ptr_array_add (array, path);

        path = g_build_filename (g_get_home_dir (),
                                 ".thumbnails",
                                 "fail",
                                 "gnome-thumbnail-factory",
                                 NULL);
        g_ptr_array_add (array, path);

        g_ptr_array_add (array, NULL);

        return (char **) g_ptr_array_free (array, FALSE);
}

static void
purge_thumbnail_cache (GsdHousekeepingManager *manager)
{

        char     **paths;
        GList     *files;
        PurgeData  purge_data;
        GTimeVal   current_time;
        guint      i;

        g_debug ("housekeeping: checking thumbnail cache size and freshness");

        purge_data.max_age = g_settings_get_int (manager->priv->settings, THUMB_AGE_KEY) * 24 * 60 * 60;
        purge_data.max_size = g_settings_get_int (manager->priv->settings, THUMB_SIZE_KEY) * 1024 * 1024;

        /* if both are set to -1, we don't need to read anything */
        if ((purge_data.max_age < 0) && (purge_data.max_size < 0))
                return;

        paths = get_thumbnail_dirs ();
        files = NULL;
        for (i = 0; paths[i] != NULL; i++)
                files = read_dir_for_purge (paths[i], files);
        g_strfreev (paths);

        g_get_current_time (&current_time);

        purge_data.now = current_time.tv_sec;
        purge_data.total_size = 0;

        if (purge_data.max_age >= 0)
                g_list_foreach (files, (GFunc) purge_old_thumbnails, &purge_data);

        if ((purge_data.total_size > purge_data.max_size) && (purge_data.max_size >= 0)) {
                GList *scan;
                files = g_list_sort (files, (GCompareFunc) sort_file_mtime);
                for (scan = files; scan && (purge_data.total_size > purge_data.max_size); scan = scan->next) {
                        ThumbData *info = scan->data;
                        g_unlink (info->path);
                        purge_data.total_size -= info->size;
                }
        }

        g_list_foreach (files, (GFunc) thumb_data_free, NULL);
        g_list_free (files);
}

static gboolean
do_cleanup (GsdHousekeepingManager *manager)
{
        purge_thumbnail_cache (manager);
        return TRUE;
}

static gboolean
do_cleanup_once (GsdHousekeepingManager *manager)
{
        do_cleanup (manager);
        manager->priv->short_term_cb = 0;
        return FALSE;
}

static void
do_cleanup_soon (GsdHousekeepingManager *manager)
{
        if (manager->priv->short_term_cb == 0) {
                g_debug ("housekeeping: will tidy up in 2 minutes");
                manager->priv->short_term_cb = g_timeout_add_seconds (INTERVAL_TWO_MINUTES,
                                               (GSourceFunc) do_cleanup_once,
                                               manager);
                g_source_set_name_by_id (manager->priv->short_term_cb, "[gnome-settings-daemon] do_cleanup_once");
        }
}

static void
settings_changed_callback (GSettings              *settings,
                           const char             *key,
                           GsdHousekeepingManager *manager)
{
        do_cleanup_soon (manager);
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
        GDateTime *now;
        now = g_date_time_new_now_local ();
        if (g_strcmp0 (method_name, "EmptyTrash") == 0) {
                gsd_ldsm_purge_trash (now);
                g_dbus_method_invocation_return_value (invocation, NULL);
        }
        else if (g_strcmp0 (method_name, "RemoveTempFiles") == 0) {
                gsd_ldsm_purge_temp_files (now);
                g_dbus_method_invocation_return_value (invocation, NULL);
        }
        g_date_time_unref (now);
}

static const GDBusInterfaceVTable interface_vtable =
{
        handle_method_call,
        NULL, /* Get Property */
        NULL, /* Set Property */
};

static void
on_bus_gotten (GObject                *source_object,
               GAsyncResult           *res,
               GsdHousekeepingManager *manager)
{
        GDBusConnection *connection;
        GError *error = NULL;
        GDBusInterfaceInfo **infos;
        int i;

        connection = g_bus_get_finish (res, &error);
        if (connection == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Could not get session bus: %s", error->message);
                g_error_free (error);
                return;
        }
        manager->priv->connection = connection;

        infos = manager->priv->introspection_data->interfaces;
        for (i = 0; infos[i] != NULL; i++) {
                g_dbus_connection_register_object (connection,
                                                   GSD_HOUSEKEEPING_DBUS_PATH,
                                                   infos[i],
                                                   &interface_vtable,
                                                   manager,
                                                   NULL,
                                                   NULL);
        }
}

static void
register_manager_dbus (GsdHousekeepingManager *manager)
{
        manager->priv->introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
        g_assert (manager->priv->introspection_data != NULL);
        manager->priv->bus_cancellable = g_cancellable_new ();

        g_bus_get (G_BUS_TYPE_SESSION,
                   manager->priv->bus_cancellable,
                   (GAsyncReadyCallback) on_bus_gotten,
                   manager);
}

gboolean
gsd_housekeeping_manager_start (GsdHousekeepingManager *manager,
                                GError                **error)
{
        gchar *dir;

        g_debug ("Starting housekeeping manager");
        gnome_settings_profile_start (NULL);

        /* Create ~/.local/ as early as possible */
        g_mkdir_with_parents(g_get_user_data_dir (), 0700);

        /* Create ~/.local/share/applications/, see
         * https://bugzilla.gnome.org/show_bug.cgi?id=703048 */
        dir = g_build_filename (g_get_user_data_dir (), "applications", NULL);
        g_mkdir (dir, 0700);
        g_free (dir);

        gsd_ldsm_setup (FALSE);

        manager->priv->settings = g_settings_new (THUMB_PREFIX);
        g_signal_connect (G_OBJECT (manager->priv->settings), "changed",
                          G_CALLBACK (settings_changed_callback), manager);

        /* Clean once, a few minutes after start-up */
        do_cleanup_soon (manager);

        /* Clean periodically, on a daily basis. */
        manager->priv->long_term_cb = g_timeout_add_seconds (INTERVAL_ONCE_A_DAY,
                                      (GSourceFunc) do_cleanup,
                                      manager);
        g_source_set_name_by_id (manager->priv->long_term_cb, "[gnome-settings-daemon] do_cleanup");

        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_housekeeping_manager_stop (GsdHousekeepingManager *manager)
{
        GsdHousekeepingManagerPrivate *p = manager->priv;

        g_debug ("Stopping housekeeping manager");

        g_clear_object (&p->bus_cancellable);
        g_clear_pointer (&p->introspection_data, g_dbus_node_info_unref);
        g_clear_object (&p->connection);

        if (p->short_term_cb) {
                g_source_remove (p->short_term_cb);
                p->short_term_cb = 0;
        }

        if (p->long_term_cb) {
                g_source_remove (p->long_term_cb);
                p->long_term_cb = 0;

                /* Do a clean-up on shutdown if and only if the size or age
                   limits have been set to paranoid levels (zero) */
                if ((g_settings_get_int (p->settings, THUMB_AGE_KEY) == 0) ||
                    (g_settings_get_int (p->settings, THUMB_SIZE_KEY) == 0)) {
                        do_cleanup (manager);
                }

        }

        g_clear_object (&p->settings);
        gsd_ldsm_clean ();
}

static void
gsd_housekeeping_manager_finalize (GObject *object)
{
        gsd_housekeeping_manager_stop (GSD_HOUSEKEEPING_MANAGER (object));

        G_OBJECT_CLASS (gsd_housekeeping_manager_parent_class)->finalize (object);
}

static void
gsd_housekeeping_manager_class_init (GsdHousekeepingManagerClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_housekeeping_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdHousekeepingManagerPrivate));
}

static void
gsd_housekeeping_manager_init (GsdHousekeepingManager *manager)
{
        manager->priv = GSD_HOUSEKEEPING_MANAGER_GET_PRIVATE (manager);
}

GsdHousekeepingManager *
gsd_housekeeping_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_HOUSEKEEPING_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);

                register_manager_dbus (manager_object);
        }

        return GSD_HOUSEKEEPING_MANAGER (manager_object);
}
