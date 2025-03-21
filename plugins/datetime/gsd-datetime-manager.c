/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Kalev Lember <kalevlember@gmail.com>
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
#include <glib/gi18n.h>
#include <libnotify/notify.h>

#include "gsd-datetime-manager.h"
#include "gsd-timezone-monitor.h"
#include "gnome-settings-profile.h"

#define DATETIME_SCHEMA "org.gnome.desktop.datetime"
#define AUTO_TIMEZONE_KEY "automatic-timezone"

struct _GsdDatetimeManager
{
        GsdApplication parent;

        GSettings *settings;
        GsdTimezoneMonitor *timezone_monitor;
        NotifyNotification *notification;
};

static void gsd_datetime_manager_class_init (GsdDatetimeManagerClass *klass);
static void gsd_datetime_manager_init (GsdDatetimeManager *manager);

G_DEFINE_TYPE (GsdDatetimeManager, gsd_datetime_manager, GSD_TYPE_APPLICATION)

static void
notification_closed_cb (NotifyNotification *n,
                        GsdDatetimeManager *self)
{
        g_clear_object (&self->notification);
}

static void
open_settings_cb (NotifyNotification *n,
                  const char         *action,
                  const char         *path)
{
        const gchar *argv[] = { "gnome-control-center", "datetime", NULL };

        g_debug ("Running gnome-control-center datetime");
        g_spawn_async (NULL, (gchar **) argv, NULL, G_SPAWN_SEARCH_PATH,
                       NULL, NULL, NULL, NULL);

        notify_notification_close (n, NULL);
}

static void
timezone_changed_cb (GsdTimezoneMonitor *timezone_monitor,
                     const gchar        *timezone_id,
                     GsdDatetimeManager *self)
{
        GDateTime *datetime;
        GTimeZone *tz;
        gchar *notification_summary;
        gchar *timezone_name;
        gchar *utc_offset;

        tz = g_time_zone_new_identifier (timezone_id);
        if (tz == NULL) {
                g_warning ("Failed to parse new timezone identifier ‘%s’. Ignoring.", timezone_id);
                return;
        }

        datetime = g_date_time_new_now (tz);
        g_time_zone_unref (tz);

        /* Translators: UTC here means the Coordinated Universal Time.
         * %:::z will be replaced by the offset from UTC e.g. UTC+02 */
        utc_offset = g_date_time_format (datetime, _("UTC%:::z"));
        timezone_name = g_strdup (g_date_time_get_timezone_abbreviation (datetime));
        g_date_time_unref (datetime);

        notification_summary = g_strdup_printf (_("Time Zone Updated to %s (%s)"),
                                                timezone_name,
                                                utc_offset);
        g_free (timezone_name);
        g_free (utc_offset);

        if (self->notification == NULL) {
                self->notification = notify_notification_new (notification_summary, NULL, NULL);
                g_signal_connect (self->notification,
                                  "closed",
                                  G_CALLBACK (notification_closed_cb),
                                  self);

                notify_notification_add_action (self->notification,
                                                "settings",
                                                _("Settings"),
                                                (NotifyActionCallback) open_settings_cb,
                                                NULL, NULL);
        } else {
                notify_notification_update (self->notification,
                                            notification_summary, NULL, NULL);
        }
        g_free (notification_summary);

        notify_notification_set_app_name (self->notification, _("Date & Time Settings"));
        notify_notification_set_hint_string (self->notification, "desktop-entry", "gnome-datetime-panel");
        notify_notification_set_urgency (self->notification, NOTIFY_URGENCY_NORMAL);
        notify_notification_set_timeout (self->notification, NOTIFY_EXPIRES_NEVER);

        if (!notify_notification_show (self->notification, NULL)) {
                g_warning ("Failed to send timezone notification");
        }
}

static void
auto_timezone_settings_changed_cb (GSettings          *settings,
                                   const char         *key,
                                   GsdDatetimeManager *self)
{
        gboolean enabled;

        enabled = g_settings_get_boolean (settings, key);
        if (enabled && self->timezone_monitor == NULL) {
                g_debug ("Automatic timezone enabled");
                self->timezone_monitor = gsd_timezone_monitor_new ();

                g_signal_connect (self->timezone_monitor, "timezone-changed",
                                  G_CALLBACK (timezone_changed_cb), self);
        } else {
                g_debug ("Automatic timezone disabled");
                g_clear_object (&self->timezone_monitor);
        }
}

static void
gsd_datetime_manager_startup (GApplication *app)
{
        GsdDatetimeManager *self = GSD_DATETIME_MANAGER (app);

        g_debug ("Starting datetime manager");
        gnome_settings_profile_start (NULL);

        self->settings = g_settings_new (DATETIME_SCHEMA);

        g_signal_connect (self->settings, "changed::" AUTO_TIMEZONE_KEY,
                          G_CALLBACK (auto_timezone_settings_changed_cb), self);
        auto_timezone_settings_changed_cb (self->settings, AUTO_TIMEZONE_KEY, self);

        G_APPLICATION_CLASS (gsd_datetime_manager_parent_class)->startup (app);

        gnome_settings_profile_end (NULL);
}

static void
gsd_datetime_manager_shutdown (GApplication *app)
{
        GsdDatetimeManager *self = GSD_DATETIME_MANAGER (app);

        g_debug ("Stopping datetime manager");

        g_clear_object (&self->settings);
        g_clear_object (&self->timezone_monitor);

        if (self->notification != NULL) {
                g_signal_handlers_disconnect_by_func (self->notification,
                                                      G_CALLBACK (notification_closed_cb),
                                                      self);
                g_clear_object (&self->notification);
        }

        G_APPLICATION_CLASS (gsd_datetime_manager_parent_class)->shutdown (app);
}

static void
gsd_datetime_manager_class_init (GsdDatetimeManagerClass *klass)
{
        GApplicationClass *application_class = G_APPLICATION_CLASS (klass);

        application_class->startup = gsd_datetime_manager_startup;
        application_class->shutdown = gsd_datetime_manager_shutdown;

        notify_init ("gnome-settings-daemon");
}

static void
gsd_datetime_manager_init (GsdDatetimeManager *manager)
{
}
