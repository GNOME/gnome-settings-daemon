/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2005-2011 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/wait.h>
#include <math.h>
#include <glib.h>
#include <glib/gi18n.h>

#include "gnome-settings-bus.h"
#include "gpm-common.h"
#include "gsd-power-constants.h"
#include "gsd-power-manager.h"

#define UPS_SOUND_LOOP_ID                        99
#define GSD_POWER_MANAGER_CRITICAL_ALERT_TIMEOUT  5 /* seconds */

static int
gsd_power_backlight_convert_safe (int value, int from_range, int to_range)
{
        /* round (value / from_range) * to_range */
        return (value * to_range + from_range / 2) / from_range;
}

/* take a discrete value with offset and convert to percentage */
int
gsd_power_backlight_abs_to_percentage (int min, int max, int value)
{
        g_return_val_if_fail (max > min, -1);
        g_return_val_if_fail (value >= min, -1);
        g_return_val_if_fail (value <= max, -1);
        return gsd_power_backlight_convert_safe (value - min, max - min, 100);
}

/* take a percentage and convert to a discrete value with offset */
int
gsd_power_backlight_percentage_to_abs (int min, int max, int value)
{
        g_return_val_if_fail (max > min, -1);
        g_return_val_if_fail (value >= 0, -1);
        g_return_val_if_fail (value <= 100, -1);

        return min + gsd_power_backlight_convert_safe (value, 100, max - min);
}

#define GPM_UP_TIME_PRECISION                   5*60
#define GPM_UP_TEXT_MIN_TIME                    120

/**
 * Return value: The time string, e.g. "2 hours 3 minutes"
 **/
gchar *
gpm_get_timestring (guint time_secs)
{
        char* timestring = NULL;
        gint  hours;
        gint  minutes;

        /* Add 0.5 to do rounding */
        minutes = (int) ( ( time_secs / 60.0 ) + 0.5 );

        if (minutes == 0) {
                timestring = g_strdup (_("Unknown time"));
                return timestring;
        }

        if (minutes < 60) {
                timestring = g_strdup_printf (ngettext ("%i minute",
                                                        "%i minutes",
                                                        minutes), minutes);
                return timestring;
        }

        hours = minutes / 60;
        minutes = minutes % 60;
        if (minutes == 0)
                timestring = g_strdup_printf (ngettext (
                                "%i hour",
                                "%i hours",
                                hours), hours);
        else
                /* TRANSLATOR: "%i %s %i %s" are "%i hours %i minutes"
                 * Swap order with "%2$s %2$i %1$s %1$i if needed */
                timestring = g_strdup_printf (_("%i %s %i %s"),
                                hours, ngettext ("hour", "hours", hours),
                                minutes, ngettext ("minute", "minutes", minutes));
        return timestring;
}

static gboolean
parse_vm_kernel_cmdline (gboolean *is_virtual_machine)
{
        gboolean ret = FALSE;
        GRegex *regex;
        GMatchInfo *match;
        char *contents;
        char *word;
        const char *arg;

        if (!g_file_get_contents ("/proc/cmdline", &contents, NULL, NULL))
                return ret;

        regex = g_regex_new ("gnome.is_vm=(\\S+)", 0, G_REGEX_MATCH_NOTEMPTY, NULL);
        if (!g_regex_match (regex, contents, G_REGEX_MATCH_NOTEMPTY, &match))
                goto out;

        word = g_match_info_fetch (match, 0);
        g_debug ("Found command-line match '%s'", word);
        arg = word + strlen ("gnome.is_vm=");
        if (*arg != '0' && *arg != '1') {
                g_warning ("Invalid value '%s' for gnome.is_vm passed in kernel command line.\n", arg);
        } else {
                *is_virtual_machine = atoi (arg);
                ret = TRUE;
        }
        g_free (word);

out:
        g_match_info_free (match);
        g_regex_unref (regex);
        g_free (contents);

        if (ret)
                g_debug ("Kernel command-line parsed to %d", *is_virtual_machine);

        return ret;
}

gboolean
gsd_power_is_hardware_a_vm (void)
{
        const gchar *str;
        gboolean ret = FALSE;
        GError *error = NULL;
        GVariant *inner;
        GVariant *variant = NULL;
        GDBusConnection *connection;

        if (parse_vm_kernel_cmdline (&ret))
                return ret;

        connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM,
                                     NULL,
                                     &error);
        if (connection == NULL) {
                g_warning ("system bus not available: %s", error->message);
                g_error_free (error);
                goto out;
        }
        variant = g_dbus_connection_call_sync (connection,
                                               "org.freedesktop.systemd1",
                                               "/org/freedesktop/systemd1",
                                               "org.freedesktop.DBus.Properties",
                                               "Get",
                                               g_variant_new ("(ss)",
                                                              "org.freedesktop.systemd1.Manager",
                                                              "Virtualization"),
                                               NULL,
                                               G_DBUS_CALL_FLAGS_NONE,
                                               -1,
                                               NULL,
                                               &error);
        if (variant == NULL) {
                g_debug ("Failed to get property '%s': %s", "Virtualization", error->message);
                g_error_free (error);
                goto out;
        }

        /* on bare-metal hardware this is the empty string,
         * otherwise an identifier such as "kvm", "vmware", etc. */
        g_variant_get (variant, "(v)", &inner);
        str = g_variant_get_string (inner, NULL);
        if (str != NULL && str[0] != '\0')
                ret = TRUE;
        g_variant_unref (inner);
out:
        if (connection != NULL)
                g_object_unref (connection);
        if (variant != NULL)
                g_variant_unref (variant);
        return ret;
}

static gpointer
parse_mock_mock_external_monitor (gpointer data)
{
	const char *mocked_file;
	mocked_file = g_getenv ("GSD_MOCK_EXTERNAL_MONITOR_FILE");

	return g_strdup (mocked_file);
}

static const gchar *
get_mock_external_monitor_file (void)
{
	  static GOnce mocked_once = G_ONCE_INIT;
	  g_once (&mocked_once, parse_mock_mock_external_monitor, NULL);
	  return mocked_once.retval;
}

static void
mock_monitor_changed (GFileMonitor     *monitor,
		      GFile            *file,
		      GFile            *other_file,
		      GFileMonitorEvent event_type)
{
        GsdDisplayConfig *display_config =
                gnome_settings_bus_get_display_config_proxy ();

	g_debug ("Emitting mocked has-external-monitor property changed signal on %p", display_config);
	g_object_notify (G_OBJECT (display_config), "has-external-monitor");
}

void
watch_external_monitor (void)
{
	const gchar *filename;
	GFile *file;
	GFileMonitor *monitor;

	filename = get_mock_external_monitor_file ();
	if (!filename)
		return;

	file = g_file_new_for_commandline_arg (filename);
	monitor = g_file_monitor (file, G_FILE_MONITOR_NONE, NULL, NULL);
	g_object_unref (file);
	g_signal_connect (monitor, "changed",
			  G_CALLBACK (mock_monitor_changed), NULL);
}

static gboolean
mock_external_monitor_is_connected (void)
{
	char *mock_external_monitor_contents;
	const gchar *filename;

	filename = get_mock_external_monitor_file ();
	g_assert (filename);

	if (g_file_get_contents (filename, &mock_external_monitor_contents, NULL, NULL)) {
		if (mock_external_monitor_contents[0] == '1') {
			g_free (mock_external_monitor_contents);
			g_debug ("Mock external monitor is on");
			return TRUE;
		} else if (mock_external_monitor_contents[0] == '0') {
			g_free (mock_external_monitor_contents);
			g_debug ("Mock external monitor is off");
			return FALSE;
		}

		g_error ("Unhandled value for GSD_MOCK_EXTERNAL_MONITOR contents: %s", mock_external_monitor_contents);
		g_free (mock_external_monitor_contents);
	}

	return FALSE;
}

gboolean
external_monitor_is_connected (void)
{
        GsdDisplayConfig *display_config =
                gnome_settings_bus_get_display_config_proxy ();
        GDBusConnection *connection;
        g_autoptr (GError) error = NULL;
        g_autoptr (GVariant) variant = NULL;
        g_autoptr (GVariant) inner = NULL;

        if (get_mock_external_monitor_file ())
                return mock_external_monitor_is_connected ();

        /* This needs to be a synchronous call, an up to date state is needed
         * in response to lid changes. */

        connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (display_config));
        variant = g_dbus_connection_call_sync (connection,
                                               "org.gnome.Mutter.DisplayConfig",
                                               "/org/gnome/Mutter/DisplayConfig",
                                               "org.freedesktop.DBus.Properties",
                                               "Get",
                                               g_variant_new ("(ss)",
                                                              "org.gnome.Mutter.DisplayConfig",
                                                              "HasExternalMonitor"),
                                               NULL,
                                               G_DBUS_CALL_FLAGS_NONE,
                                               -1,
                                               NULL,
                                               &error);
        if (!variant) {
                g_debug ("Failed to get property 'HasExternalMonitor': %s", error->message);
                return FALSE;
        }

        g_variant_get (variant, "(v)", &inner);
        return g_variant_get_boolean (inner);
}

static void
play_sound (ca_context *ca_context)
{
        ca_context_play (ca_context, UPS_SOUND_LOOP_ID,
                         CA_PROP_EVENT_ID, "battery-caution",
                         CA_PROP_EVENT_DESCRIPTION, _("Battery is critically low"), NULL);
}

static gboolean
play_loop_timeout_cb (gpointer user_data)
{
        play_sound (user_data);
        return TRUE;
}

void
play_loop_start (ca_context *ca_context,
                 guint      *id)
{
        if (*id != 0)
                return;

        *id = g_timeout_add_seconds (GSD_POWER_MANAGER_CRITICAL_ALERT_TIMEOUT,
                                     (GSourceFunc) play_loop_timeout_cb,
                                     ca_context);
        g_source_set_name_by_id (*id, "[gnome-settings-daemon] play_loop_timeout_cb");
        play_sound (ca_context);
}

void
play_loop_stop (ca_context *ca_context,
                guint      *id)
{
        if (*id == 0)
                return;

        ca_context_cancel (ca_context, UPS_SOUND_LOOP_ID);
        g_source_remove (*id);
        *id = 0;
}
