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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

/* gcc -DHAVE_LIBNOTIFY -DTEST -Wall `pkg-config --cflags --libs gobject-2.0 gio-unix-2.0 glib-2.0 gtk+-2.0 libnotify` -o gsd-disk-space-test gsd-disk-space.c */

#include <sys/statvfs.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gio/gunixmounts.h>
#include <gtk/gtk.h>

#ifdef HAVE_LIBNOTIFY

#include <libnotify/notify.h>

/*
 * TODO:
 *  + gconf to make it possible to customize the define below (?)
 */

#define FREE_PERCENT_NOTIFY        0.05
#define FREE_PERCENT_NOTIFY_AGAIN  0.01
/* No notification if there's more than 2 GB */
#define FREE_SIZE_GB_NO_NOTIFY     2
#define GIGABYTE                   1024 * 1024 * 1024

#ifdef TEST
#undef  FREE_PERCENT_NOTIFY
#define FREE_PERCENT_NOTIFY        0.95
#undef  FREE_SIZE_GB_NO_NOTIFY
#define FREE_SIZE_GB_NO_NOTIFY     200
#endif

#define CHECK_EVERY_X_SECONDS      60

#define DISK_SPACE_ANALYZER        "baobab"

static GHashTable        *ldsm_notified_hash = NULL;
static unsigned int       ldsm_timeout_id = 0;
static GUnixMountMonitor *ldsm_monitor = NULL;

static void
ldsm_hash_free_slice_gdouble (gpointer data)
{
        g_slice_free (gdouble, data);
}

static char *
ldsm_get_icon_name_from_g_icon (GIcon *gicon)
{
        const char * const *names;
        GtkIconTheme *icon_theme;
        int i;

        if (!G_IS_THEMED_ICON (gicon))
                return NULL;

        names = g_themed_icon_get_names (G_THEMED_ICON (gicon));
        icon_theme = gtk_icon_theme_get_default ();

        for (i = 0; names[i] != NULL; i++) {
                if (gtk_icon_theme_has_icon (icon_theme, names[i]))
                        return g_strdup (names[i]);
        }

        return NULL;
}

static void
ldsm_notification_clicked (NotifyNotification *notification,
                           const char         *action,
                           const char         *path)
{
        const char *argv[] = { DISK_SPACE_ANALYZER, path, NULL };

        if (strcmp (action, "analyze") != 0) {
                return;
        }

        g_spawn_async (NULL, (char **) argv, NULL, G_SPAWN_SEARCH_PATH,
                       NULL, NULL, NULL, NULL);
}

static void
ldsm_notify_for_mount (GUnixMountEntry *mount,
                       double           free_space,
                       gboolean         has_disk_analyzer)
{
        char  *name;
        char  *msg;
        GIcon *gicon;
        char  *icon;
        int    in_use;
        NotifyNotification *notif;

        name = g_unix_mount_guess_name (mount);
        in_use = 100 - (free_space * 100);
        msg = g_strdup_printf (_("%d%% of the disk space on `%s' is in use"),
                               in_use, name);
        g_free (name);

        gicon = g_unix_mount_guess_icon (mount);
        icon = ldsm_get_icon_name_from_g_icon (gicon);
        g_object_unref (gicon);

        notif = notify_notification_new (_("Low Disk Space"), msg, icon, NULL);
        g_free (msg);
        g_free (icon);

        notify_notification_set_urgency (notif, NOTIFY_URGENCY_CRITICAL);

        if (has_disk_analyzer) {
                const char *path;

                path = g_unix_mount_get_mount_path (mount);

                notify_notification_add_action (notif, "analyze", _("Analyze"),
                                                (NotifyActionCallback) ldsm_notification_clicked,
                                                g_strdup (path), g_free);
        }

        g_signal_connect (notif, "closed", G_CALLBACK (g_object_unref), NULL);

        notify_notification_show (notif, NULL);
}

static void
ldsm_check_mount (GUnixMountEntry *mount,
                  gboolean         has_disk_analyzer)
{
        const char *path;
        struct statvfs buf;
        unsigned long threshold_blocks;
        double free_space;
        double previous_free_space;
        double *previous_free_space_p;

        path = g_unix_mount_get_mount_path (mount);

        /* get the old stats we saved for this mount in case we notified */
        previous_free_space_p = g_hash_table_lookup (ldsm_notified_hash, path);
        if (previous_free_space_p != NULL)
                previous_free_space = *previous_free_space_p;
        else
                previous_free_space = 0;

        if (statvfs (path, &buf) != 0) {
                g_hash_table_remove (ldsm_notified_hash, path);
                return;
        }

        /* not a real filesystem, but a virtual one. Skip it */
        if (buf.f_blocks == 0) {
                g_hash_table_remove (ldsm_notified_hash, path);
                return;
        }

        free_space = (double) buf.f_bavail / (double) buf.f_blocks;
        /* enough free space, nothing to do */
        if (free_space > FREE_PERCENT_NOTIFY) {
                g_hash_table_remove (ldsm_notified_hash, path);
                return;
        }

        /* note that we try to avoid doing an overflow */
        threshold_blocks = FREE_SIZE_GB_NO_NOTIFY * (GIGABYTE / buf.f_bsize);
        /* more than enough space, nothing to do */
        if (buf.f_bavail > threshold_blocks) {
                g_hash_table_remove (ldsm_notified_hash, path);
                return;
        }

        /* did we already notify the user? If yes, we only notify if the disk
         * is getting more and more filled */
        if (previous_free_space != 0 &&
            previous_free_space - free_space < FREE_PERCENT_NOTIFY_AGAIN) {
                return;
        }

        ldsm_notify_for_mount (mount, free_space, has_disk_analyzer);

        /* replace the information about the latest notification */
        previous_free_space_p = g_slice_new (gdouble);
        *previous_free_space_p = free_space;
        g_hash_table_replace (ldsm_notified_hash,
                              g_strdup (path), previous_free_space_p);
}

static gboolean
ldsm_check_all_mounts (gpointer data)
{
        GList *mounts;
        GList *l;
        GHashTable *seen;
        char *program;
        gboolean has_disk_analyzer;

        program = g_find_program_in_path (DISK_SPACE_ANALYZER);
        has_disk_analyzer = (program != NULL);
        g_free (program);

        /* it's possible to get duplicate mounts, and we don't want duplicate
         * notifications */
        seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
        mounts = g_unix_mounts_get (NULL);

        for (l = mounts; l != NULL; l = l->next) {
                GUnixMountEntry *mount = l->data;
                const char *path;

                if (g_unix_mount_is_readonly (mount)) {
                        g_unix_mount_free (mount);
                        continue;
                }

                path = g_unix_mount_get_mount_path (mount);

                if (g_hash_table_lookup_extended (seen, path, NULL, NULL)) {
                        g_unix_mount_free (mount);
                        continue;
                }

                g_hash_table_insert (seen,
                                     g_strdup (path), GINT_TO_POINTER(1));

                ldsm_check_mount (mount, has_disk_analyzer);

                g_unix_mount_free (mount);
        }

        g_hash_table_destroy (seen);

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
        mounts = g_unix_mounts_get (NULL);
        g_hash_table_foreach_remove (ldsm_notified_hash,
                                     ldsm_is_hash_item_not_in_mounts, mounts);
        g_list_foreach (mounts, (GFunc) g_unix_mount_free, NULL);

        /* check the status now, for the new mounts */
        ldsm_check_all_mounts (NULL);

        /* and reset the timeout */
        if (ldsm_timeout_id)
                g_source_remove (ldsm_timeout_id);
        ldsm_timeout_id = g_timeout_add_seconds (CHECK_EVERY_X_SECONDS,
                                                 ldsm_check_all_mounts, NULL);
}

void
gsd_ldsm_setup (gboolean check_now)
{
        if (ldsm_notified_hash || ldsm_timeout_id || ldsm_monitor) {
                g_warning ("Low disk space monitor already initialized.");
                return;
        }

        if (!notify_is_initted ()) {
                if (!notify_init ("Low Disk Space Monitor"))
                        return;
        }

        ldsm_notified_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                    g_free,
                                                    ldsm_hash_free_slice_gdouble);

        ldsm_monitor = g_unix_mount_monitor_new ();
        g_unix_mount_monitor_set_rate_limit (ldsm_monitor, 1000);
        g_signal_connect (ldsm_monitor, "mounts-changed",
                          G_CALLBACK (ldsm_mounts_changed), NULL);

        if (check_now)
                ldsm_check_all_mounts (NULL);

        ldsm_timeout_id = g_timeout_add_seconds (CHECK_EVERY_X_SECONDS,
                                                 ldsm_check_all_mounts, NULL);
}

void
gsd_ldsm_clean (void)
{
        if (ldsm_timeout_id)
                g_source_remove (ldsm_timeout_id);
        ldsm_timeout_id = 0;

        if (ldsm_notified_hash)
                g_hash_table_destroy (ldsm_notified_hash);
        ldsm_notified_hash = NULL;

        if (ldsm_monitor)
                g_object_unref (ldsm_monitor);
        ldsm_monitor = NULL;
}

#else  /* HAVE_LIBNOTIFY */

void
gsd_ldsm_setup (gboolean check_now)
{
}

void
gsd_ldsm_clean (void)
{
}

#endif /* HAVE_LIBNOTIFY */

#ifdef TEST
int
main (int    argc,
      char **argv)
{
        GMainLoop *loop;

        gtk_init (&argc, &argv);

        loop = g_main_loop_new (NULL, FALSE);

        gsd_ldsm_setup (TRUE);

        g_main_loop_run (loop);

        gsd_ldsm_clean ();
        g_main_loop_unref (loop);

        return 0;
}
#endif /* TEST */
