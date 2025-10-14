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

#define DONATE_URL "https://donate.gnome.org"
#define DONATE_SCHEMA "org.gnome.settings-daemon.plugins.housekeeping"
#define DONATE_LAST_SHOWN_KEY "donation-reminder-last-shown"
#define DONATE_ENABLED_KEY "donation-reminder-enabled"
#define INITIAL_DELAY (60 * 60)
#define DAY_IN_SEC (24 * 60 * 60)
#define HALF_A_YEAR_IN_USEC ((int64_t) 365 * DAY_IN_SEC * G_USEC_PER_SEC)

static NotifyNotification *notification = NULL;
static guint check_timeout_id = 0;
static gboolean first_run = TRUE;

static void
closed_cb (NotifyNotification *n)
{
	g_assert (n == notification);
	g_clear_object (&notification);
}

static void
donate_cb (NotifyNotification *n)
{
	g_autoptr (GAppLaunchContext) context = NULL;
	g_autoptr (GError) error = NULL;

	g_assert (n == notification);

	context = g_app_launch_context_new ();

	if (!g_app_info_launch_default_for_uri (DONATE_URL,
						context,
						&error))
		g_warning ("Failed to open link: %s", error->message);

	notify_notification_close (notification, NULL);
}

static void
show_notification (void)
{
	if (notification)
		return;

        notification = notify_notification_new (_("Support GNOME"),
						_("GNOME needs your help. Your donation will sustain "
						  "our open source project for future generations."),
						NULL);
        g_signal_connect (notification,
                          "closed",
                          G_CALLBACK (closed_cb),
                          NULL);

        notify_notification_set_app_name (notification, _("GNOME"));
        notify_notification_set_hint (notification, "transient", g_variant_new_boolean (TRUE));
        notify_notification_set_urgency (notification, NOTIFY_URGENCY_NORMAL);
        notify_notification_set_timeout (notification, NOTIFY_EXPIRES_DEFAULT);
        notify_notification_set_hint_string (notification, "desktop-entry", "gnome-about-panel");

        notify_notification_add_action (notification,
                                        "donate",
                                        _("Donate"),
                                        (NotifyActionCallback) donate_cb,
                                        NULL,
                                        NULL);

        notify_notification_set_category (notification, "device");

        if (!notify_notification_show (notification, NULL)) {
                g_warning ("failed to send disk space notification\n");
        }
}

static void
check_show_notification (GSettings *settings)
{
	int64_t timestamp, now;

	timestamp = g_settings_get_int64 (settings, DONATE_LAST_SHOWN_KEY);

	now = g_get_real_time ();

	if ((now - HALF_A_YEAR_IN_USEC) > timestamp) {
		g_settings_set_int64 (settings, DONATE_LAST_SHOWN_KEY, now);
		show_notification ();
	}
}

static gboolean
show_notification_timeout_cb (void)
{
	g_autoptr (GSettings) settings = NULL;

	settings = g_settings_new (DONATE_SCHEMA);

        check_show_notification (settings);
        first_run = FALSE;

        return G_SOURCE_CONTINUE;
}

void
gsd_donation_reminder_init (void)
{
	g_autoptr (GSettings) settings = NULL;

	settings = g_settings_new (DONATE_SCHEMA);

        if (!g_settings_get_boolean (settings, DONATE_ENABLED_KEY))
                return;

	if (check_timeout_id == 0) {
		check_timeout_id =
			g_timeout_add_seconds (first_run ? INITIAL_DELAY : DAY_IN_SEC,
					       (GSourceFunc) show_notification_timeout_cb,
					       NULL);
	}
}

void
gsd_donation_reminder_end (void)
{
	g_clear_handle_id (&check_timeout_id, g_source_remove);
	g_clear_object (&notification);
}
