/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * vim: set et sw=8 ts=8:
 *
 * Copyright (c) 2008, Novell, Inc.
 *
 * Authors: Vincent Untz <vuntz@gnome.org>
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

#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gio/gunixmounts.h>
#include <gio/gio.h>
#include <libnotify/notify.h>

#include "gsd-disk-space.h"
#include "gsd-disk-space-helper.h"
#include "gnome-settings-systemd.h"

#define GIGABYTE                   1024 * 1024 * 1024

#define CHECK_EVERY_X_SECONDS      60

#define DISK_SPACE_ANALYZER        "baobab"

#define SETTINGS_HOUSEKEEPING_DIR     "org.gnome.settings-daemon.plugins.housekeeping"
#define SETTINGS_FREE_PC_NOTIFY_KEY   "free-percent-notify"
#define SETTINGS_FREE_PC_NOTIFY_AGAIN_KEY "free-percent-notify-again"
#define SETTINGS_FREE_SIZE_NO_NOTIFY  "free-size-gb-no-notify"
#define SETTINGS_MIN_NOTIFY_PERIOD    "min-notify-period"
#define SETTINGS_IGNORE_PATHS         "ignore-paths"

#define PRIVACY_SETTINGS              "org.gnome.desktop.privacy"
#define SETTINGS_PURGE_TRASH          "remove-old-trash-files"
#define SETTINGS_PURGE_TEMP_FILES     "remove-old-temp-files"
#define SETTINGS_PURGE_AFTER          "old-files-age"

typedef struct
{
        GUnixMountEntry *mount;
        struct statvfs buf;
        time_t notify_time;
} LdsmMountInfo;

static GHashTable        *ldsm_notified_hash = NULL;
static unsigned int       ldsm_timeout_id = 0;
static GUnixMountMonitor *ldsm_monitor = NULL;
static double             free_percent_notify = 0.05;
static double             free_percent_notify_again = 0.01;
static unsigned int       free_size_gb_no_notify = 2;
static unsigned int       min_notify_period = 10;
static GSList            *ignore_paths = NULL;
static GSettings         *settings = NULL;
static GSettings         *privacy_settings = NULL;
static NotifyNotification *notification = NULL;

static guint64           *time_read;

static gboolean           purge_trash;
static gboolean           purge_temp_files;
static guint              purge_after;
static guint              purge_trash_id = 0;
static guint              purge_temp_id = 0;

static gchar*
ldsm_get_fs_id_for_path (const gchar *path)
{
        GFile *file;
        GFileInfo *fileinfo;
        gchar *attr_id_fs;

        file = g_file_new_for_path (path);
        fileinfo = g_file_query_info (file, G_FILE_ATTRIBUTE_ID_FILESYSTEM, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
        if (fileinfo) {
                attr_id_fs = g_strdup (g_file_info_get_attribute_string (fileinfo, G_FILE_ATTRIBUTE_ID_FILESYSTEM));
                g_object_unref (fileinfo);
        } else {
                attr_id_fs = NULL;
        }

        g_object_unref (file);

        return attr_id_fs;
}

static gboolean
ldsm_mount_has_trash (const char *path)
{
        const gchar *user_data_dir;
        gchar *user_data_attr_id_fs;
        gchar *path_attr_id_fs;
        gboolean mount_uses_user_trash = FALSE;
        gchar *trash_files_dir;
        gboolean has_trash = FALSE;
        GDir *dir;

        user_data_dir = g_get_user_data_dir ();
        user_data_attr_id_fs = ldsm_get_fs_id_for_path (user_data_dir);

        path_attr_id_fs = ldsm_get_fs_id_for_path (path);

        if (g_strcmp0 (user_data_attr_id_fs, path_attr_id_fs) == 0) {
                /* The volume that is low on space is on the same volume as our home
                 * directory. This means the trash is at $XDG_DATA_HOME/Trash,
                 * not at the root of the volume which is full.
                 */
                mount_uses_user_trash = TRUE;
        }

        g_free (user_data_attr_id_fs);
        g_free (path_attr_id_fs);

        /* I can't think of a better way to find out if a volume has any trash. Any suggestions? */
        if (mount_uses_user_trash) {
                trash_files_dir = g_build_filename (g_get_user_data_dir (), "Trash", "files", NULL);
        } else {
                gchar *uid;

                uid = g_strdup_printf ("%d", getuid ());
                trash_files_dir = g_build_filename (path, ".Trash", uid, "files", NULL);
                if (!g_file_test (trash_files_dir, G_FILE_TEST_IS_DIR)) {
                        gchar *trash_dir;

                        g_free (trash_files_dir);
                        trash_dir = g_strdup_printf (".Trash-%s", uid);
                        trash_files_dir = g_build_filename (path, trash_dir, "files", NULL);
                        g_free (trash_dir);
                        if (!g_file_test (trash_files_dir, G_FILE_TEST_IS_DIR)) {
                                g_free (trash_files_dir);
                                g_free (uid);
                                return has_trash;
                        }
                }
                g_free (uid);
        }

        dir = g_dir_open (trash_files_dir, 0, NULL);
        if (dir) {
                if (g_dir_read_name (dir))
                        has_trash = TRUE;
                g_dir_close (dir);
        }

        g_free (trash_files_dir);

        return has_trash;
}

static void
ldsm_analyze_path (const gchar *path)
{
        const gchar *argv[] = { DISK_SPACE_ANALYZER, path, NULL };

        g_spawn_async (NULL, (gchar **) argv, NULL, G_SPAWN_SEARCH_PATH,
                        NULL, NULL, NULL, NULL);
}

static void
ignore_callback (NotifyNotification *n,
                 const char         *action)
{
        g_assert (action != NULL);
        g_assert (strcmp (action, "ignore") == 0);

        /* Do nothing */

        notify_notification_close (n, NULL);
}

static void
examine_callback (NotifyNotification *n,
                  const char         *action,
                  const char         *path)
{
        g_assert (action != NULL);
        g_assert (strcmp (action, "examine") == 0);

        ldsm_analyze_path (path);

        notify_notification_close (n, NULL);
}

static gboolean
should_purge_file (GFile        *file,
                   GCancellable *cancellable,
                   GDateTime    *old)
{
        GFileInfo *info;
        GDateTime *date;
        gboolean should_purge;

        should_purge = FALSE;

        info = g_file_query_info (file,
                                  G_FILE_ATTRIBUTE_TRASH_DELETION_DATE ","
                                  G_FILE_ATTRIBUTE_UNIX_UID ","
                                  G_FILE_ATTRIBUTE_TIME_CHANGED,
                                  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                  cancellable,
                                  NULL);
        if (!info)
                return FALSE;

        date = g_file_info_get_deletion_date (info);
        if (date == NULL) {
                guint uid;
                guint64 ctime;

                uid = g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_UID);
                if (uid != getuid ()) {
                        should_purge = FALSE;
                        goto out;
                }

                ctime = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_CHANGED);
                date = g_date_time_new_from_unix_local ((gint64) ctime);
        }

        should_purge = g_date_time_difference (old, date) >= 0;
        g_date_time_unref (date);

out:
        g_object_unref (info);

        return should_purge;
}

DeleteData *
delete_data_new (GFile        *file,
                 GCancellable *cancellable,
                 GDateTime    *old,
                 gboolean      dry_run,
                 gboolean      trash,
                 gint          depth,
                 const char   *filesystem)
{
        DeleteData *data;

        data = g_new (DeleteData, 1);
        g_ref_count_init (&data->ref_count);
        data->file = g_object_ref (file);
        data->filesystem = g_strdup (filesystem);
        data->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
        data->old = g_date_time_ref (old);
        data->dry_run = dry_run;
        data->trash = trash;
        data->depth = depth;
        data->name = g_file_get_parse_name (data->file);

        return data;
}

char*
get_filesystem (GFile *file)
{
        g_autoptr(GFileInfo) info = NULL;

        info = g_file_query_info (file,
                                  G_FILE_ATTRIBUTE_ID_FILESYSTEM,
                                  G_FILE_QUERY_INFO_NONE,
                                  NULL,
                                  NULL);

        return g_file_info_get_attribute_as_string (info,
                                                    G_FILE_ATTRIBUTE_ID_FILESYSTEM);
}

static DeleteData *
delete_data_ref (DeleteData *data)
{
        g_ref_count_inc (&data->ref_count);

        return data;
}

void
delete_data_unref (DeleteData *data)
{
        if (g_ref_count_dec (&data->ref_count)) {
                g_object_unref (data->file);
                if (data->cancellable)
                        g_object_unref (data->cancellable);
                g_date_time_unref (data->old);
                g_free (data->name);
                g_free (data->filesystem);
                g_free (data);
        }
}

static void
delete_batch (GObject      *source,
              GAsyncResult *res,
              gpointer      user_data)
{
        GFileEnumerator *enumerator = G_FILE_ENUMERATOR (source);
        DeleteData *data = user_data;
        const char *fs;
        GList *files, *f;
        GFile *child_file;
        DeleteData *child;
        GFileInfo *info;
        GError *error = NULL;

        files = g_file_enumerator_next_files_finish (enumerator, res, &error);

        g_debug ("GsdHousekeeping: purging %d children of %s", g_list_length (files), data->name);

        if (files) {
                for (f = files; f; f = f->next) {
                        if (g_cancellable_is_cancelled (data->cancellable))
                                break;
                        info = f->data;

                        fs = g_file_info_get_attribute_string (info,
                                                               G_FILE_ATTRIBUTE_ID_FILESYSTEM);

                        /* Do not consider the file if it is on a different file system.
                         * Ignore data->filesystem if it is NULL (this only happens if
                         * it is the toplevel trash directory). */
                        if (data->filesystem && g_strcmp0 (fs, data->filesystem) != 0) {
                                g_debug ("GsdHousekeeping: skipping file \"%s\" as it is on a different file system",
                                         g_file_info_get_name (info));
                                continue;
                        }

                        child_file = g_file_get_child (data->file, g_file_info_get_name (info));
                        child = delete_data_new (child_file,
                                                 data->cancellable,
                                                 data->old,
                                                 data->dry_run,
                                                 data->trash,
                                                 data->depth + 1,
                                                 fs);
                        delete_recursively_by_age (child);
                        delete_data_unref (child);
                        g_object_unref (child_file);
                }
                g_list_free_full (files, g_object_unref);
                if (!g_cancellable_is_cancelled (data->cancellable)) {
                        g_file_enumerator_next_files_async (enumerator, 20,
                                                            0,
                                                            data->cancellable,
                                                            delete_batch,
                                                            data);
                        return;
                }
        }

        g_file_enumerator_close (enumerator, data->cancellable, NULL);
        g_object_unref (enumerator);

        if (data->depth > 0 && !g_cancellable_is_cancelled (data->cancellable)) {
                if ((data->trash && data->depth > 1) ||
                     should_purge_file (data->file, data->cancellable, data->old)) {
                        g_debug ("GsdHousekeeping: purging %s\n", data->name);
                        if (!data->dry_run) {
                                g_file_delete (data->file, data->cancellable, NULL);
                        }
                }
        }
        delete_data_unref (data);
}

static void
delete_subdir (GObject      *source,
               GAsyncResult *res,
               gpointer      user_data)
{
        GFile *file = G_FILE (source);
        DeleteData *data = user_data;
        GFileEnumerator *enumerator;
        GError *error = NULL;

        g_debug ("GsdHousekeeping: purging %s in %s\n",
                 data->trash ? "trash" : "temporary files", data->name);

        enumerator = g_file_enumerate_children_finish (file, res, &error);
        if (error) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY) &&
                    !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
                    !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED))
                        g_warning ("Failed to enumerate children of %s: %s\n", data->name, error->message);
        }
        if (enumerator) {
                g_file_enumerator_next_files_async (enumerator, 20,
                                                    0,
                                                    data->cancellable,
                                                    delete_batch,
                                                    delete_data_ref (data));
        } else if (data->depth > 0 && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY)) {
                if ((data->trash && data->depth > 1) ||
                     should_purge_file (data->file, data->cancellable, data->old)) {
                        g_debug ("Purging %s leaf node", data->name);
                        if (!data->dry_run) {
                                g_file_delete (data->file, data->cancellable, NULL);
                        }
                }
        }

        if (error)
                g_error_free (error);
        delete_data_unref (data);
}

static void
delete_subdir_check_symlink (GObject      *source,
                             GAsyncResult *res,
                             gpointer      user_data)
{
        GFile *file = G_FILE (source);
        DeleteData *data = user_data;
        GFileInfo *info;

        info = g_file_query_info_finish (file, res, NULL);
        if (!info) {
                delete_data_unref (data);
                return;
        }

        if (g_file_info_get_file_type (info) == G_FILE_TYPE_SYMBOLIC_LINK) {
                if (should_purge_file (data->file, data->cancellable, data->old)) {
                        g_debug ("Purging %s leaf node", data->name);
                        if (!data->dry_run) {
                                g_file_delete (data->file, data->cancellable, NULL);
                        }
                }
        } else if (g_strcmp0 (g_file_info_get_name (info), ".X11-unix") == 0) {
                g_debug ("Skipping X11 socket directory");
        } else {
                g_file_enumerate_children_async (data->file,
                                                 G_FILE_ATTRIBUTE_STANDARD_NAME ","
                                                 G_FILE_ATTRIBUTE_STANDARD_TYPE ","
                                                 G_FILE_ATTRIBUTE_ID_FILESYSTEM,
                                                 G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                                 0,
                                                 data->cancellable,
                                                 delete_subdir,
                                                 delete_data_ref (data));
        }
        g_object_unref (info);
        delete_data_unref (data);
}

void
delete_recursively_by_age (DeleteData *data)
{
        if (data->trash && (data->depth == 1) &&
            !should_purge_file (data->file, data->cancellable, data->old)) {
                /* no need to recurse into trashed directories */
                return;
        }

        g_file_query_info_async (data->file,
                                 G_FILE_ATTRIBUTE_STANDARD_NAME ","
                                 G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                 G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                 0,
                                 data->cancellable,
                                 delete_subdir_check_symlink,
                                 delete_data_ref (data));
}

void
gsd_ldsm_purge_trash (GDateTime *old)
{
        GFile *file;
        DeleteData *data;

        file = g_file_new_for_uri ("trash:");
        data = delete_data_new (file, NULL, old, FALSE, TRUE, 0, NULL);
        delete_recursively_by_age (data);
        delete_data_unref (data);
        g_object_unref (file);
}

void
gsd_ldsm_purge_temp_files (GDateTime *old)
{
        DeleteData *data;
        GFile *file;
        char *filesystem;

        /* Never clean temporary files on a sane (i.e. systemd managed)
         * system. In that case systemd already ships
         *   /usr/lib/tmpfiles.d/tmp.conf
         * which does the trick in a much safer way.
         * Ideally we can just drop this feature, I am not sure why it was
         * added in the first place though, it does not really seem like a
         * privacy feature (also, it was late in the release cycle).
         *   https://en.wikipedia.org/wiki/Wikipedia:Chesterton%27s_fence
         */
        if (gnome_settings_have_systemd ())
                return;

        file = g_file_new_for_path (g_get_tmp_dir ());
        filesystem = get_filesystem (file);
        data = delete_data_new (file, NULL, old, FALSE, FALSE, 0, filesystem);
        g_free (filesystem);
        delete_recursively_by_age (data);
        delete_data_unref (data);
        g_object_unref (file);

        if (g_strcmp0 (g_get_tmp_dir (), "/var/tmp") != 0) {
                file = g_file_new_for_path ("/var/tmp");
                filesystem = get_filesystem (file);
                data = delete_data_new (file, NULL, old, FALSE, FALSE, 0, filesystem);
                g_free (filesystem);
                delete_recursively_by_age (data);
                delete_data_unref (data);
                g_object_unref (file);
        }

        if (g_strcmp0 (g_get_tmp_dir (), "/tmp") != 0) {
                file = g_file_new_for_path ("/tmp");
                filesystem = get_filesystem (file);
                data = delete_data_new (file, NULL, old, FALSE, FALSE, 0, filesystem);
                g_free (filesystem);
                delete_recursively_by_age (data);
                delete_data_unref (data);
                g_object_unref (file);
        }
}

void
gsd_ldsm_show_empty_trash (void)
{
        GFile *file;
        GDateTime *old;
        DeleteData *data;

        old = g_date_time_new_now_local ();
        file = g_file_new_for_uri ("trash:");
        data = delete_data_new (file, NULL, old, TRUE, TRUE, 0, NULL);
        g_object_unref (file);
        g_date_time_unref (old);

        delete_recursively_by_age (data);
        delete_data_unref (data);
}

static gboolean
ldsm_purge_trash_and_temp (gpointer data)
{
        GDateTime *now, *old;

        now = g_date_time_new_now_local ();
        old = g_date_time_add_days (now, - purge_after);

        if (purge_trash) {
                g_debug ("housekeeping: purge trash older than %u days", purge_after);
                gsd_ldsm_purge_trash (old);
        }
        if (purge_temp_files) {
                g_debug ("housekeeping: purge temp files older than %u days", purge_after);
                gsd_ldsm_purge_temp_files (old);
        }

        g_date_time_unref (old);
        g_date_time_unref (now);

        return G_SOURCE_CONTINUE;
}

static void
empty_trash_callback (NotifyNotification *n,
                      const char         *action)
{
        GDateTime *old;

        g_assert (action != NULL);
        g_assert (strcmp (action, "empty-trash") == 0);

        old = g_date_time_new_now_local ();
        gsd_ldsm_purge_trash (old);
        g_date_time_unref (old);

        notify_notification_close (n, NULL);
}

static void
on_notification_closed (NotifyNotification *n)
{
        g_object_unref (notification);
        notification = NULL;
}

static void
ldsm_notify (const char *summary,
             const char *body,
             const char *mount_path)
{
        gchar *program;
        gboolean has_disk_analyzer;
        gboolean has_trash;

        /* Don't show a notice if one is already displayed */
        if (notification != NULL)
                return;

        notification = notify_notification_new (summary, body, NULL);
        g_signal_connect (notification,
                          "closed",
                          G_CALLBACK (on_notification_closed),
                          NULL);

        notify_notification_set_app_name (notification, _("Disk Space"));
        notify_notification_set_hint (notification, "transient", g_variant_new_boolean (TRUE));
        notify_notification_set_urgency (notification, NOTIFY_URGENCY_CRITICAL);
        notify_notification_set_timeout (notification, NOTIFY_EXPIRES_DEFAULT);
        notify_notification_set_hint_string (notification, "desktop-entry", "org.gnome.baobab");
        notify_notification_set_hint (notification, "image-path", g_variant_new_string ("drive-harddisk-symbolic"));


        program = g_find_program_in_path (DISK_SPACE_ANALYZER);
        has_disk_analyzer = (program != NULL);
        g_free (program);

        if (has_disk_analyzer) {
                notify_notification_add_action (notification,
                                                "examine",
                                                _("Examine"),
                                                (NotifyActionCallback) examine_callback,
                                                g_strdup (mount_path),
                                                g_free);
        }

        has_trash = ldsm_mount_has_trash (mount_path);

        if (has_trash) {
                notify_notification_add_action (notification,
                                                "empty-trash",
                                                _("Empty Trash"),
                                                (NotifyActionCallback) empty_trash_callback,
                                                NULL,
                                                NULL);
        }

        notify_notification_add_action (notification,
                                        "ignore",
                                        _("Ignore"),
                                        (NotifyActionCallback) ignore_callback,
                                        NULL,
                                        NULL);
        notify_notification_set_category (notification, "device");

        if (!notify_notification_show (notification, NULL)) {
                g_warning ("failed to send disk space notification\n");
        }
}

static void
ldsm_notify_for_mount (LdsmMountInfo *mount,
                       gboolean       multiple_volumes)
{
        gboolean has_trash;
        gchar  *name;
        gint64 free_space;
        const gchar *path;
        char *free_space_str;
        char *summary;
        char *body;

        name = g_unix_mount_guess_name (mount->mount);
        path = g_unix_mount_get_mount_path (mount->mount);
        has_trash = ldsm_mount_has_trash (path);

        free_space = (gint64) mount->buf.f_frsize * (gint64) mount->buf.f_bavail;
        free_space_str = g_format_size (free_space);

        if (multiple_volumes) {
                summary = g_strdup_printf (_("Low Disk Space on “%s”"), name);
                if (has_trash) {
                        body = g_strdup_printf (_("The volume “%s” has only %s disk space remaining.  You may free up some space by emptying the trash."),
                                                name,
                                                free_space_str);
                } else {
                        body = g_strdup_printf (_("The volume “%s” has only %s disk space remaining."),
                                                name,
                                                free_space_str);
                }
        } else {
                summary = g_strdup (_("Low Disk Space"));
                if (has_trash) {
                        body = g_strdup_printf (_("This computer has only %s disk space remaining.  You may free up some space by emptying the trash."),
                                                free_space_str);
                } else {
                        body = g_strdup_printf (_("This computer has only %s disk space remaining."),
                                                free_space_str);
                }
        }

        ldsm_notify (summary, body, path);

        g_free (free_space_str);
        g_free (summary);
        g_free (body);
        g_free (name);
}

static gboolean
ldsm_mount_has_space (LdsmMountInfo *mount)
{
        gdouble free_space;

        free_space = (double) mount->buf.f_bavail / (double) mount->buf.f_blocks;
        /* enough free space, nothing to do */
        if (free_space > free_percent_notify)
                return TRUE;

        if (((gint64) mount->buf.f_frsize * (gint64) mount->buf.f_bavail) > ((gint64) free_size_gb_no_notify * GIGABYTE))
                return TRUE;

        /* If we got here, then this volume is low on space */
        return FALSE;
}

static gboolean
ldsm_mount_is_virtual (LdsmMountInfo *mount)
{
        if (mount->buf.f_blocks == 0) {
                /* Filesystems with zero blocks are virtual */
                return TRUE;
        }

        return FALSE;
}

static gint
ldsm_ignore_path_compare (gconstpointer a,
                          gconstpointer b)
{
        return g_strcmp0 ((const gchar *)a, (const gchar *)b);
}

static gboolean
ldsm_mount_is_user_ignore (const gchar *path)
{
        if (g_slist_find_custom (ignore_paths, path, (GCompareFunc) ldsm_ignore_path_compare) != NULL)
                return TRUE;
        else
                return FALSE;
}


static void
ldsm_free_mount_info (gpointer data)
{
        LdsmMountInfo *mount = data;

        g_return_if_fail (mount != NULL);

        g_unix_mount_free (mount->mount);
        g_free (mount);
}

static void
ldsm_maybe_warn_mounts (GList *mounts,
                        gboolean multiple_volumes)
{
        GList *l;
        gboolean done = FALSE;

        for (l = mounts; l != NULL; l = l->next) {
                LdsmMountInfo *mount_info = l->data;
                LdsmMountInfo *previous_mount_info;
                gdouble free_space;
                gdouble previous_free_space;
                time_t curr_time;
                const gchar *path;
                gboolean show_notify;

                if (done) {
                        /* Don't show any more dialogs if the user took action with the last one. The user action
                         * might free up space on multiple volumes, making the next dialog redundant.
                         */
                        ldsm_free_mount_info (mount_info);
                        continue;
                }

                path = g_unix_mount_get_mount_path (mount_info->mount);

                previous_mount_info = g_hash_table_lookup (ldsm_notified_hash, path);
                if (previous_mount_info != NULL)
                        previous_free_space = (gdouble) previous_mount_info->buf.f_bavail / (gdouble) previous_mount_info->buf.f_blocks;

                free_space = (gdouble) mount_info->buf.f_bavail / (gdouble) mount_info->buf.f_blocks;

                if (previous_mount_info == NULL) {
                        /* We haven't notified for this mount yet */
                        show_notify = TRUE;
                        mount_info->notify_time = time (NULL);
                        g_hash_table_replace (ldsm_notified_hash, g_strdup (path), mount_info);
                } else if ((previous_free_space - free_space) > free_percent_notify_again) {
                        /* We've notified for this mount before and free space has decreased sufficiently since last time to notify again */
                        curr_time = time (NULL);
                        if (difftime (curr_time, previous_mount_info->notify_time) > (gdouble)(min_notify_period * 60)) {
                                show_notify = TRUE;
                                mount_info->notify_time = curr_time;
                        } else {
                                /* It's too soon to show the dialog again. However, we still replace the LdsmMountInfo
                                 * struct in the hash table, but give it the notfiy time from the previous dialog.
                                 * This will stop the notification from reappearing unnecessarily as soon as the timeout expires.
                                 */
                                show_notify = FALSE;
                                mount_info->notify_time = previous_mount_info->notify_time;
                        }
                        g_hash_table_replace (ldsm_notified_hash, g_strdup (path), mount_info);
                } else {
                        /* We've notified for this mount before, but the free space hasn't decreased sufficiently to notify again */
                        ldsm_free_mount_info (mount_info);
                        show_notify = FALSE;
                }

                if (show_notify) {
                        ldsm_notify_for_mount (mount_info, multiple_volumes);
                        done = TRUE;
                }
        }
}

static gboolean
ldsm_check_all_mounts (gpointer data)
{
        GList *mounts;
        GList *l;
        GList *check_mounts = NULL;
        GList *full_mounts = NULL;
        guint number_of_mounts = 0;
        gboolean multiple_volumes = FALSE;

        /* We iterate through the static mounts in /etc/fstab first, seeing if
         * they're mounted by checking if the GUnixMountPoint has a corresponding GUnixMountEntry.
         * Iterating through the static mounts means we automatically ignore dynamically mounted media.
         */
        mounts = g_unix_mount_points_get (time_read);

        for (l = mounts; l != NULL; l = l->next) {
                GUnixMountPoint *mount_point = l->data;
                GUnixMountEntry *mount;
                LdsmMountInfo *mount_info;
                const gchar *path;

                path = g_unix_mount_point_get_mount_path (mount_point);
                mount = g_unix_mount_at (path, time_read);
                g_unix_mount_point_free (mount_point);
                if (mount == NULL) {
                        /* The GUnixMountPoint is not mounted */
                        continue;
                }

                mount_info = g_new0 (LdsmMountInfo, 1);
                mount_info->mount = mount;

                path = g_unix_mount_get_mount_path (mount);

                if (g_unix_mount_is_readonly (mount)) {
                        ldsm_free_mount_info (mount_info);
                        continue;
                }

                if (ldsm_mount_is_user_ignore (path)) {
                        ldsm_free_mount_info (mount_info);
                        continue;
                }

                if (gsd_should_ignore_unix_mount (mount)) {
                        ldsm_free_mount_info (mount_info);
                        continue;
                }

                if (statvfs (path, &mount_info->buf) != 0) {
                        ldsm_free_mount_info (mount_info);
                        continue;
                }

                if (ldsm_mount_is_virtual (mount_info)) {
                        ldsm_free_mount_info (mount_info);
                        continue;
                }

                check_mounts = g_list_prepend (check_mounts, mount_info);
                number_of_mounts += 1;
        }

        g_list_free (mounts);

        if (number_of_mounts > 1)
                multiple_volumes = TRUE;

        for (l = check_mounts; l != NULL; l = l->next) {
                LdsmMountInfo *mount_info = l->data;

                if (!ldsm_mount_has_space (mount_info)) {
                        full_mounts = g_list_prepend (full_mounts, mount_info);
                } else {
                        g_hash_table_remove (ldsm_notified_hash, g_unix_mount_get_mount_path (mount_info->mount));
                        ldsm_free_mount_info (mount_info);
                }
        }

        ldsm_maybe_warn_mounts (full_mounts, multiple_volumes);

        g_list_free (check_mounts);
        g_list_free (full_mounts);

        return TRUE;
}

static gboolean
ldsm_is_hash_item_not_in_mounts (gpointer key,
                                 gpointer value,
                                 gpointer user_data)
{
        GList *l;

        for (l = (GList *) user_data; l != NULL; l = l->next) {
                GUnixMountEntry *mount = l->data;
                const char *path;

                path = g_unix_mount_get_mount_path (mount);

                if (strcmp (path, key) == 0)
                        return FALSE;
        }

        return TRUE;
}

static void
ldsm_mounts_changed (GObject  *monitor,
                     gpointer  data)
{
        GList *mounts;

        /* remove the saved data for mounts that got removed */
        mounts = g_unix_mounts_get (time_read);
        g_hash_table_foreach_remove (ldsm_notified_hash,
                                     ldsm_is_hash_item_not_in_mounts, mounts);
        g_list_free_full (mounts, (GDestroyNotify) g_unix_mount_free);

        /* check the status now, for the new mounts */
        ldsm_check_all_mounts (NULL);

        /* and reset the timeout */
        if (ldsm_timeout_id)
                g_source_remove (ldsm_timeout_id);
        ldsm_timeout_id = g_timeout_add_seconds (CHECK_EVERY_X_SECONDS,
                                                 ldsm_check_all_mounts, NULL);
        g_source_set_name_by_id (ldsm_timeout_id, "[gnome-settings-daemon] ldsm_check_all_mounts");
}

static gboolean
ldsm_is_hash_item_in_ignore_paths (gpointer key,
                                   gpointer value,
                                   gpointer user_data)
{
        return ldsm_mount_is_user_ignore (key);
}

static void
gsd_ldsm_get_config (void)
{
        gchar **settings_list;

        free_percent_notify = g_settings_get_double (settings, SETTINGS_FREE_PC_NOTIFY_KEY);
        free_percent_notify_again = g_settings_get_double (settings, SETTINGS_FREE_PC_NOTIFY_AGAIN_KEY);

        free_size_gb_no_notify = g_settings_get_int (settings, SETTINGS_FREE_SIZE_NO_NOTIFY);
        min_notify_period = g_settings_get_int (settings, SETTINGS_MIN_NOTIFY_PERIOD);

        if (ignore_paths != NULL) {
                g_slist_foreach (ignore_paths, (GFunc) g_free, NULL);
                g_clear_pointer (&ignore_paths, g_slist_free);
        }

        settings_list = g_settings_get_strv (settings, SETTINGS_IGNORE_PATHS);
        if (settings_list != NULL) {
                guint i;

                for (i = 0; settings_list[i] != NULL; i++)
                        ignore_paths = g_slist_prepend (ignore_paths, g_strdup (settings_list[i]));

                /* Make sure we dont leave stale entries in ldsm_notified_hash */
                g_hash_table_foreach_remove (ldsm_notified_hash,
                                             ldsm_is_hash_item_in_ignore_paths, NULL);

                g_strfreev (settings_list);
        }

        purge_trash = g_settings_get_boolean (privacy_settings, SETTINGS_PURGE_TRASH);
        purge_temp_files = g_settings_get_boolean (privacy_settings, SETTINGS_PURGE_TEMP_FILES);
        purge_after = g_settings_get_uint (privacy_settings, SETTINGS_PURGE_AFTER);
}

static void
gsd_ldsm_update_config (GSettings *settings,
                        const gchar *key,
                        gpointer user_data)
{
        gsd_ldsm_get_config ();
}

void
gsd_ldsm_setup (gboolean check_now)
{
        if (ldsm_notified_hash || ldsm_timeout_id || ldsm_monitor) {
                g_warning ("Low disk space monitor already initialized.");
                return;
        }

        ldsm_notified_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                    g_free,
                                                    ldsm_free_mount_info);

        settings = g_settings_new (SETTINGS_HOUSEKEEPING_DIR);
        privacy_settings = g_settings_new (PRIVACY_SETTINGS);
        gsd_ldsm_get_config ();
        g_signal_connect (G_OBJECT (settings), "changed",
                          G_CALLBACK (gsd_ldsm_update_config), NULL);

        ldsm_monitor = g_unix_mount_monitor_get ();
        g_signal_connect (ldsm_monitor, "mounts-changed",
                          G_CALLBACK (ldsm_mounts_changed), NULL);

        if (check_now)
                ldsm_check_all_mounts (NULL);

        ldsm_timeout_id = g_timeout_add_seconds (CHECK_EVERY_X_SECONDS,
                                                 ldsm_check_all_mounts, NULL);
        g_source_set_name_by_id (ldsm_timeout_id, "[gnome-settings-daemon] ldsm_check_all_mounts");

        purge_trash_id = g_timeout_add_seconds (3600, ldsm_purge_trash_and_temp, NULL);
        g_source_set_name_by_id (purge_trash_id, "[gnome-settings-daemon] ldsm_purge_trash_and_temp");
}

void
gsd_ldsm_clean (void)
{
        if (purge_trash_id)
                g_source_remove (purge_trash_id);
        purge_trash_id = 0;

        if (purge_temp_id)
                g_source_remove (purge_temp_id);
        purge_temp_id = 0;

        if (ldsm_timeout_id)
                g_source_remove (ldsm_timeout_id);
        ldsm_timeout_id = 0;

        g_clear_pointer (&ldsm_notified_hash, g_hash_table_destroy);
        g_clear_object (&ldsm_monitor);
        g_clear_object (&settings);
        g_clear_object (&privacy_settings);
        /* NotifyNotification::closed callback will drop reference */
        if (notification != NULL)
                notify_notification_close (notification, NULL);
        g_slist_free_full (ignore_paths, g_free);
        ignore_paths = NULL;
}

