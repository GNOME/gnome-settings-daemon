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
#include <libnotify/notify.h>

#include "gnome-settings-profile.h"
#include "gsd-housekeeping-manager.h"
#include "gsd-disk-space.h"
#include "gsd-donation-reminder.h"
#include "gsd-systemd-notify.h"


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

struct _GsdHousekeepingManager {
        GsdApplication parent;

        GSettings *settings;
        guint long_term_cb;
        guint short_term_cb;

        GDBusNodeInfo   *introspection_data;

        GsdSystemdNotify *systemd_notify;
};

static void     gsd_housekeeping_manager_class_init  (GsdHousekeepingManagerClass *klass);
static void     gsd_housekeeping_manager_init        (GsdHousekeepingManager      *housekeeping_manager);

G_DEFINE_TYPE (GsdHousekeepingManager, gsd_housekeeping_manager, GSD_TYPE_APPLICATION)

typedef struct {
        GDateTime *now;  /* (owned) */
        GTimeSpan max_age_us;
        goffset total_size;
        goffset max_size;
} PurgeData;


typedef struct {
        GDateTime *time;  /* (owned) (nullable) */
        char   *path;
        glong   size;
} ThumbData;


static void
thumb_data_free (gpointer data)
{
        ThumbData *info = data;

        if (info) {
                g_clear_pointer (&info->time, g_date_time_unref);
                g_free (info->path);
                g_free (info);
        }
}

static GList *
read_dir_for_purge (const char *path, GList *files)
{
        GFile           *read_path;
        GFileEnumerator *enum_dir;
        int              cannot_get_time = 0;

        read_path = g_file_new_for_path (path);
        enum_dir = g_file_enumerate_children (read_path,
                                              G_FILE_ATTRIBUTE_STANDARD_NAME ","
                                              G_FILE_ATTRIBUTE_TIME_ACCESS ","
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
                                GDateTime *time = NULL;

                                entry = g_file_get_child (read_path, name);
                                entry_path = g_file_get_path (entry);
                                g_object_unref (entry);

                                // If atime is available, it should be no worse than mtime.
                                // - Even if the file system is mounted with noatime, the atime and
                                //   mtime will be set to the same value on file creation.
                                // - Since the thumbnailer never edits thumbnails, and instead swaps
                                //   in newly created temp files, atime will always be >= mtime.

                                if (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_TIME_ACCESS)) {
                                    time = g_file_info_get_access_date_time (info);
                                } else if (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_TIME_MODIFIED)) {
                                    time = g_file_info_get_modification_date_time (info);
                                } else {
                                    // Unlikely to get here, but be defensive
                                    cannot_get_time += 1;
                                    time = NULL;
                                }

                                td = g_new0 (ThumbData, 1);
                                td->path = entry_path;
                                td->time = g_steal_pointer (&time);
                                td->size = g_file_info_get_size (info);

                                files = g_list_prepend (files, td);
                        }
                        g_object_unref (info);
                }
                g_object_unref (enum_dir);

                if (cannot_get_time > 0) {
                    g_warning ("Could not read atime or mtime on %d files in %s", cannot_get_time, path);
                }
        }
        g_object_unref (read_path);

        return files;
}

static void
purge_old_thumbnails (ThumbData *info, PurgeData *purge_data)
{
        if (info->time != NULL &&
            g_date_time_difference (purge_data->now, info->time) > purge_data->max_age_us) {
                g_unlink (info->path);
                info->size = 0;
        } else {
                purge_data->total_size += info->size;
        }
}

static int
sort_file_time (ThumbData *file1, ThumbData *file2)
{
        if (file1->time == NULL && file2->time == NULL)
                return 0;
        else if (file1->time == NULL)
                return 1;
        else if (file2->time == NULL)
                return -1;
        else
                return g_date_time_difference (file1->time, file2->time);
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
                                 "x-large",
                                 NULL);
        g_ptr_array_add (array, path);

        path = g_build_filename (g_get_user_cache_dir (),
                                 "thumbnails",
                                 "xx-large",
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
        guint      i;

        g_debug ("housekeeping: checking thumbnail cache size and freshness");

        purge_data.max_age_us = (GTimeSpan) g_settings_get_int (manager->settings, THUMB_AGE_KEY) * 24 * 60 * 60 * G_USEC_PER_SEC;
        purge_data.max_size = (goffset) g_settings_get_int (manager->settings, THUMB_SIZE_KEY) * 1024 * 1024;

        /* if both are set to -1, we don't need to read anything */
        if ((purge_data.max_age_us < 0) && (purge_data.max_size < 0))
                return;

        paths = get_thumbnail_dirs ();
        files = NULL;
        for (i = 0; paths[i] != NULL; i++)
                files = read_dir_for_purge (paths[i], files);
        g_strfreev (paths);

        purge_data.now = g_date_time_new_now_utc ();
        purge_data.total_size = 0;

        if (purge_data.max_age_us >= 0)
                g_list_foreach (files, (GFunc) purge_old_thumbnails, &purge_data);

        if ((purge_data.total_size > purge_data.max_size) && (purge_data.max_size >= 0)) {
                GList *scan;
                files = g_list_sort (files, (GCompareFunc) sort_file_time);
                for (scan = files; scan && (purge_data.total_size > purge_data.max_size); scan = scan->next) {
                        ThumbData *info = scan->data;
                        g_unlink (info->path);
                        purge_data.total_size -= info->size;
                }
        }

        g_list_foreach (files, (GFunc) thumb_data_free, NULL);
        g_list_free (files);
        g_date_time_unref (purge_data.now);
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
        manager->short_term_cb = 0;
        return FALSE;
}

static void
do_cleanup_soon (GsdHousekeepingManager *manager)
{
        if (manager->short_term_cb == 0) {
                g_debug ("housekeeping: will tidy up in 2 minutes");
                manager->short_term_cb = g_timeout_add_seconds (INTERVAL_TWO_MINUTES,
                                               (GSourceFunc) do_cleanup_once,
                                               manager);
                g_source_set_name_by_id (manager->short_term_cb, "[gnome-settings-daemon] do_cleanup_once");
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
gsd_housekeeping_manager_startup (GApplication *app)
{
        GsdHousekeepingManager *manager = GSD_HOUSEKEEPING_MANAGER (app);
        gchar *dir;

        g_debug ("Starting housekeeping manager");
        gnome_settings_profile_start (NULL);

        /* Create ~/.local/ as early as possible */
        (void) g_mkdir_with_parents(g_get_user_data_dir (), 0700);

        /* Create ~/.local/share/applications/, see
         * https://bugzilla.gnome.org/show_bug.cgi?id=703048 */
        dir = g_build_filename (g_get_user_data_dir (), "applications", NULL);
        (void) g_mkdir (dir, 0700);
        g_free (dir);

        gsd_ldsm_setup (FALSE);

        manager->settings = g_settings_new (THUMB_PREFIX);
        g_signal_connect (G_OBJECT (manager->settings), "changed",
                          G_CALLBACK (settings_changed_callback), manager);

        /* Clean once, a few minutes after start-up */
        do_cleanup_soon (manager);

        /* Clean periodically, on a daily basis. */
        manager->long_term_cb = g_timeout_add_seconds (INTERVAL_ONCE_A_DAY,
                                      (GSourceFunc) do_cleanup,
                                      manager);
        g_source_set_name_by_id (manager->long_term_cb, "[gnome-settings-daemon] do_cleanup");

        manager->systemd_notify = g_object_new (GSD_TYPE_SYSTEMD_NOTIFY, NULL);

        G_APPLICATION_CLASS (gsd_housekeeping_manager_parent_class)->startup (app);

        gsd_donation_reminder_init ();

        gnome_settings_profile_end (NULL);
}

static void
gsd_housekeeping_manager_shutdown (GApplication *app)
{
        GsdHousekeepingManager *manager = GSD_HOUSEKEEPING_MANAGER (app);

        g_debug ("Stopping housekeeping manager");

        gsd_donation_reminder_end ();

        g_clear_object (&manager->systemd_notify);

        if (manager->short_term_cb) {
                g_source_remove (manager->short_term_cb);
                manager->short_term_cb = 0;
        }

        if (manager->long_term_cb) {
                g_source_remove (manager->long_term_cb);
                manager->long_term_cb = 0;

                /* Do a clean-up on shutdown if and only if the size or age
                   limits have been set to paranoid levels (zero) */
                if ((g_settings_get_int (manager->settings, THUMB_AGE_KEY) == 0) ||
                    (g_settings_get_int (manager->settings, THUMB_SIZE_KEY) == 0)) {
                        do_cleanup (manager);
                }

        }

        g_clear_object (&manager->settings);
        gsd_ldsm_clean ();

        G_APPLICATION_CLASS (gsd_housekeeping_manager_parent_class)->shutdown (app);
}

static gboolean
gsd_housekeeping_manager_dbus_register (GApplication    *app,
                                        GDBusConnection *connection,
                                        const char     *object_path,
                                        GError         **error)
{
        GsdHousekeepingManager *manager = GSD_HOUSEKEEPING_MANAGER (app);
        GDBusInterfaceInfo **infos;
        int i;

        if (!G_APPLICATION_CLASS (gsd_housekeeping_manager_parent_class)->dbus_register (app,
                                                                                         connection,
                                                                                         object_path,
                                                                                         error))
                return FALSE;

        manager->introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
        g_assert (manager->introspection_data != NULL);

        infos = manager->introspection_data->interfaces;
        for (i = 0; infos[i] != NULL; i++) {
                g_dbus_connection_register_object (connection,
                                                   GSD_HOUSEKEEPING_DBUS_PATH,
                                                   infos[i],
                                                   &interface_vtable,
                                                   manager,
                                                   NULL,
                                                   NULL);
        }

        return TRUE;
}

static void
gsd_housekeeping_manager_dbus_unregister (GApplication    *app,
                                   GDBusConnection *connection,
                                   const char      *object_path)
{
        GsdHousekeepingManager *manager = GSD_HOUSEKEEPING_MANAGER (app);

        g_clear_pointer (&manager->introspection_data, g_dbus_node_info_unref);

        G_APPLICATION_CLASS (gsd_housekeeping_manager_parent_class)->dbus_unregister (app,
                                                                                      connection,
                                                                                      object_path);
}

static void
gsd_housekeeping_manager_class_init (GsdHousekeepingManagerClass *klass)
{
        GApplicationClass *application_class = G_APPLICATION_CLASS (klass);

        application_class->startup = gsd_housekeeping_manager_startup;
        application_class->shutdown = gsd_housekeeping_manager_shutdown;
        application_class->dbus_register = gsd_housekeeping_manager_dbus_register;
        application_class->dbus_unregister = gsd_housekeeping_manager_dbus_unregister;

        notify_init ("gnome-settings-daemon");
}

static void
gsd_housekeeping_manager_init (GsdHousekeepingManager *manager)
{
}
