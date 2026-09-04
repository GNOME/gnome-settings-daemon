/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * vim: set et sw=8 ts=8:
 *
 * Copyright (c) 2025 GNOME Foundation.
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
 * Authors: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "gsd-donation-reminder.h"

#include <glib/gi18n.h>
#include <libnotify/notify.h>

#define DONATE_URL_BASE "https://donate.gnome.org/?mtm_medium=notification"

#define CAMPAIGN_CHECK_URL "https://donate.gnome.org/api/campaign"

#define DONATE_SCHEMA "org.gnome.settings-daemon.plugins.housekeeping"
#define DONATE_LAST_SHOWN_KEY "donation-reminder-last-shown"
#define DONATE_ENABLED_KEY "donation-reminder-enabled"

#define GROUP_NAME "Campaign"

#define HOUR_IN_SEC (60 * 60)
#define DAY_IN_SEC (24 * 60 * 60)

typedef struct _GsdDonationReminder GsdDonationReminder;
struct _GsdDonationReminder
{
        NotifyNotification *notification;
        GSettings *settings;
        guint check_timeout_id;
        gboolean already_running;

        GKeyFile *key_file;
        GFile *local_file;
        GFile *sha_file;
        GFileMonitor *file_monitor;
        char *campaign_id;
        char *distro_id;
};

static GsdDonationReminder *reminders = NULL;

static void
closed_cb (NotifyNotification  *n,
           GsdDonationReminder *reminders)
{
        g_assert (n == reminders->notification);
        g_clear_object (&reminders->notification);
}

static void
donate_cb (NotifyNotification  *n,
           const char          *action,
           GsdDonationReminder *reminders)
{
        g_autoptr (GAppLaunchContext) context = NULL;
        g_autoptr (GError) error = NULL;
        g_autoptr (GString) url = NULL;

        g_assert (n == reminders->notification);

        context = notify_notification_get_activation_app_launch_context (n);

        url = g_string_new (DONATE_URL_BASE);

        if (reminders->campaign_id) {
                g_string_append (url, "&mtm_campaign=");
                g_string_append_uri_escaped (url,
                                             reminders->campaign_id,
                                             NULL, FALSE);
        }

        if (reminders->distro_id) {
                g_string_append (url, "&mtm_source=");
                g_string_append_uri_escaped (url,
                                             reminders->distro_id,
                                             NULL, FALSE);
        }

        if (!g_app_info_launch_default_for_uri (url->str,
                                                context,
                                                &error))
                g_warning ("Failed to open link: %s", error->message);

        notify_notification_close (n, NULL);
}

static void
show_notification (GsdDonationReminder *reminders)
{
        g_autofree char *name = NULL, *comment = NULL, *id = NULL;

        if (reminders->notification)
                return;

        name = g_key_file_get_locale_string (reminders->key_file,
                                             GROUP_NAME, "Name", NULL, NULL);
        comment = g_key_file_get_locale_string (reminders->key_file,
                                                GROUP_NAME, "Comment", NULL, NULL);

        id = g_key_file_get_string (reminders->key_file, GROUP_NAME, "Id", NULL);

        if (!name || !comment)
                return;

        reminders->notification =
                notify_notification_new (name, comment, NULL);
        g_signal_connect (reminders->notification,
                          "closed",
                          G_CALLBACK (closed_cb),
                          reminders);

        g_clear_pointer (&reminders->campaign_id, g_free);
        reminders->campaign_id = g_steal_pointer (&id);

        notify_notification_set_app_name (reminders->notification, _("GNOME"));
        notify_notification_set_urgency (reminders->notification, NOTIFY_URGENCY_NORMAL);
        notify_notification_set_timeout (reminders->notification, NOTIFY_EXPIRES_DEFAULT);
        notify_notification_set_hint_string (reminders->notification, "desktop-entry", "gnome-about-panel");

        notify_notification_add_action (reminders->notification,
                                        "donate",
                                        _("Donate"),
                                        (NotifyActionCallback) donate_cb,
                                        reminders,
                                        NULL);

        notify_notification_set_category (reminders->notification, "device");

        if (!notify_notification_show (reminders->notification, NULL))
                g_warning ("failed to send donation reminder\n");
}

static gboolean
load_key_file (GsdDonationReminder *reminders)
{
        g_autoptr (GKeyFile) key_file = NULL;
        g_autofree char *last_sha = NULL, *new_sha = NULL;

        key_file = g_key_file_new ();

        if (!g_key_file_load_from_file (key_file,
                                        g_file_peek_path (reminders->local_file),
                                        G_KEY_FILE_KEEP_COMMENTS |
                                        G_KEY_FILE_KEEP_TRANSLATIONS,
                                        NULL))
                return FALSE;

        g_clear_pointer (&reminders->key_file, g_key_file_unref);
        reminders->key_file = g_steal_pointer (&key_file);

        new_sha = g_key_file_get_string (reminders->key_file,
                                         GROUP_NAME, "Sha", NULL);
        if (!new_sha)
                return FALSE;

        g_file_get_contents (g_file_peek_path (reminders->sha_file),
                             &last_sha, NULL, NULL);

        if (g_strcmp0 (last_sha, new_sha) != 0) {
                /* New campaign, reset the last shown key */
                g_settings_set_int64 (reminders->settings, DONATE_LAST_SHOWN_KEY, 0);
                g_file_set_contents (g_file_peek_path (reminders->sha_file),
                                     new_sha, -1, NULL);
        }

        return TRUE;
}

static gboolean
should_show_notification (GsdDonationReminder *reminders,
                          GDateTime           *now)
{
        g_autofree char *start_str = NULL, *start_as_datetime = NULL;
        g_autoptr (GDateTime) start_date = NULL, last_shown = NULL, next_shown = NULL;
        g_autoptr (GTimeZone) tz = NULL;
        int64_t last_shown_time;
        int repeat_delay;

        start_str = g_key_file_get_string (reminders->key_file,
                                           GROUP_NAME, "Start", NULL);
        if (!start_str)
                return FALSE;

        repeat_delay = g_key_file_get_integer (reminders->key_file,
                                               GROUP_NAME, "RepeatDelay", NULL);
        if (repeat_delay == 0)
                return FALSE;

        tz = g_time_zone_new_local ();
        start_as_datetime = g_strdup_printf ("%sT12:00:00", start_str);
        start_date = g_date_time_new_from_iso8601 (start_as_datetime, tz);
        if (!start_date)
                return FALSE;

        last_shown_time = g_settings_get_int64 (reminders->settings,
                                                DONATE_LAST_SHOWN_KEY);
        if (last_shown_time) {
                last_shown = g_date_time_new_from_unix_local_usec (last_shown_time);
                next_shown = g_date_time_add_days (last_shown, repeat_delay);
        }

        /* The time to show this campaign did not start yet */
        if (!last_shown && g_date_time_difference (now, start_date) < 0)
                return FALSE;

        /* The campaign was already shown, it's not time to show it again yet */
        if (next_shown && g_date_time_difference (now, next_shown) < 0)
                return FALSE;

        return TRUE;
}

static void
check_show_notification (GsdDonationReminder *reminders)
{
        g_autoptr (GDateTime) now = NULL;

        now = g_date_time_new_now_local ();

        if (should_show_notification (reminders, now)) {
                show_notification (reminders);
                g_settings_set_int64 (reminders->settings,
                                      DONATE_LAST_SHOWN_KEY,
                                      g_date_time_to_unix_usec (now));
        }
}

static void
on_donations_file_changed (GFileMonitor      *monitor,
                           GFile             *file,
                           GFile             *other_file,
                           GFileMonitorEvent  event,
                           gpointer           user_data)
{
        GsdDonationReminder *reminders = user_data;

        if (event != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT)
                return;

        if (!load_key_file (reminders))
                return;

        check_show_notification (reminders);
}

static void
read_remote_file_cb (GObject      *object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
        GsdDonationReminder *reminders = user_data;
        g_autoptr (GInputStream) istream = NULL;
        g_autoptr (GOutputStream) ostream = NULL;
        g_autoptr (GFile) dir = NULL;

        istream = G_INPUT_STREAM (g_file_read_finish (G_FILE (object), res, NULL));
        if (!istream)
                return;

        dir = g_file_get_parent (reminders->local_file);

        g_file_make_directory_with_parents (dir, NULL, NULL);

        ostream = G_OUTPUT_STREAM (g_file_replace (reminders->local_file,
                                                   NULL, FALSE,
                                                   G_FILE_CREATE_REPLACE_DESTINATION,
                                                   NULL, NULL));
        if (!ostream)
                return;

        g_output_stream_splice_async (ostream,
                                      istream,
                                      G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE |
                                      G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
                                      G_PRIORITY_DEFAULT,
                                      NULL, NULL, NULL);
}

static void
download_file (GsdDonationReminder *reminders)
{
        g_autoptr (GFile) source_file = NULL;

        source_file = g_file_new_for_uri (CAMPAIGN_CHECK_URL);

        g_file_read_async (source_file, G_PRIORITY_DEFAULT,
                           NULL, read_remote_file_cb, reminders);
}

static gboolean
check_download_cb (GsdDonationReminder *reminders)
{
        download_file (reminders);

        if (reminders->already_running) {
                return G_SOURCE_CONTINUE;
        } else {
                /* Replace with a longer timeout */
                reminders->already_running = TRUE;
                g_clear_handle_id (&reminders->check_timeout_id, g_source_remove);
                reminders->check_timeout_id =
                        g_timeout_add_seconds (DAY_IN_SEC,
                                               (GSourceFunc) check_download_cb,
                                               reminders);
                return G_SOURCE_REMOVE;
        }
}

static char *
get_os_release_variable (GBytes     *bytes,
                         const char *var_name)
{
        g_autofree char *initial_match = NULL, *substr_match = NULL;
        size_t len, start_pos, end_pos;
        const char *content, *ptr;

        content = g_bytes_get_data (bytes, &len);

        initial_match = g_strconcat (var_name, "=", NULL);
        substr_match = g_strconcat ("\n", var_name, "=", NULL);

        if (len > strlen (initial_match) &&
            memcmp (content, initial_match, strlen (initial_match)) == 0) {
                start_pos = strlen (initial_match) + 1;
        } else {
                if ((ptr = memmem (content, len, substr_match, strlen (substr_match))) == NULL)
                        return NULL;

                start_pos = ptr - content + strlen (initial_match) + 1;
        }

        g_assert (start_pos <= len);
        ptr = memmem (&content[start_pos], len - start_pos, "\n", strlen ("\n"));
        end_pos = ptr ? ptr - content : len;
        g_assert (end_pos <= len);

        return g_strndup (&content[start_pos], end_pos - start_pos);
}

static char *
get_distro_id (void)
{
        g_autoptr (GMappedFile) os_release = NULL;
        g_autoptr (GBytes) bytes = NULL;
        g_autofree char *id = NULL, *version_id = NULL;

        os_release = g_mapped_file_new ("/etc/os-release", FALSE, NULL);
        if (!os_release)
                return NULL;

        bytes = g_mapped_file_get_bytes (os_release);

        id = get_os_release_variable (bytes, "ID");
        version_id = get_os_release_variable (bytes, "VERSION_ID");

        return g_strconcat (id, "-", version_id, NULL);
}

void
gsd_donation_reminder_init (void)
{
        g_autoptr (GSettings) settings = NULL;
        g_autofree char *path = NULL, *sha_path = NULL;

        if (reminders)
                return;

        settings = g_settings_new (DONATE_SCHEMA);

        if (!g_settings_get_boolean (settings, DONATE_ENABLED_KEY))
                return;

        reminders = g_new0 (GsdDonationReminder, 1);
        reminders->settings = g_steal_pointer (&settings);
        reminders->check_timeout_id =
                g_timeout_add_seconds (HOUR_IN_SEC,
                                       (GSourceFunc) check_download_cb,
                                       reminders);

        sha_path = g_build_filename (g_get_user_cache_dir (),
                                     "gnome-settings-daemon",
                                     "donations.sha",
                                     NULL);
        reminders->sha_file = g_file_new_for_path (sha_path);

        path = g_build_filename (g_get_user_cache_dir (),
                                 "gnome-settings-daemon",
                                 "donations",
                                 NULL);
        reminders->local_file = g_file_new_for_path (path);

        reminders->file_monitor = g_file_monitor_file (reminders->local_file,
                                                       G_FILE_MONITOR_NONE,
                                                       NULL, NULL);
        g_signal_connect (reminders->file_monitor,
                          "changed",
                          G_CALLBACK (on_donations_file_changed),
                          reminders);

        reminders->distro_id = get_distro_id ();
}

void
gsd_donation_reminder_end (void)
{
        if (!reminders)
                return;

        g_clear_handle_id (&reminders->check_timeout_id, g_source_remove);
        g_clear_object (&reminders->notification);
        g_clear_object (&reminders->settings);
        g_clear_object (&reminders->local_file);
        g_clear_object (&reminders->sha_file);
        g_clear_pointer (&reminders->key_file, g_key_file_unref);
        g_signal_handlers_disconnect_by_data (reminders->file_monitor, reminders);
        g_clear_object (&reminders->file_monitor);
        g_clear_pointer (&reminders->distro_id, g_free);
        g_free (reminders);
}
