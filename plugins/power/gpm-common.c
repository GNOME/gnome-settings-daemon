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
#include <gdk/gdkx.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/dpms.h>
#include <canberra-gtk.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-rr.h>

#include "gnome-settings-bus.h"
#include "gpm-common.h"
#include "gsd-power-constants.h"
#include "gsd-power-manager.h"
#include "gsd-backlight-linux.h"

#define XSCREENSAVER_WATCHDOG_TIMEOUT           120 /* seconds */
#define UPS_SOUND_LOOP_ID                        99
#define GSD_POWER_MANAGER_CRITICAL_ALERT_TIMEOUT  5 /* seconds */

enum BacklightHelperCommand {
        BACKLIGHT_HELPER_GET,
        BACKLIGHT_HELPER_GET_MAX,
        BACKLIGHT_HELPER_SET
};

/* take a discrete value with offset and convert to percentage */
int
gsd_power_backlight_abs_to_percentage (int min, int max, int value)
{
        g_return_val_if_fail (max > min, -1);
        g_return_val_if_fail (value >= min, -1);
        g_return_val_if_fail (value <= max, -1);
        return (((value - min) * 100) / (max - min));
}

/* take a percentage and convert to a discrete value with offset */
int
gsd_power_backlight_percentage_to_abs (int min, int max, int value)
{
        int steps, step_size;

        g_return_val_if_fail (max > min, -1);
        g_return_val_if_fail (value >= 0, -1);
        g_return_val_if_fail (value <= 100, -1);

        steps = max - min;
        step_size = 100 / steps;

        /* Round for better precision when steps is small */
        value += step_size / 2;

        return min + (steps * value) / 100;
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

gboolean
gsd_power_is_hardware_a_tablet (void)
{
        char *type;
        gboolean ret = FALSE;

        type = gnome_settings_get_chassis_type ();
        ret = (g_strcmp0 (type, "tablet") == 0);
        g_free (type);

        return ret;
}

/* This timer goes off every few minutes, whether the user is idle or not,
   to try and clean up anything that has gone wrong.

   It calls disable_builtin_screensaver() so that if xset has been used,
   or some other program (like xlock) has messed with the XSetScreenSaver()
   settings, they will be set back to sensible values (if a server extension
   is in use, messing with xlock can cause the screensaver to never get a wakeup
   event, and could cause monitor power-saving to occur, and all manner of
   heinousness.)

   This code was originally part of gnome-screensaver, see
   http://git.gnome.org/browse/gnome-screensaver/tree/src/gs-watcher-x11.c?id=fec00b12ec46c86334cfd36b37771cc4632f0d4d#n530
 */
static gboolean
disable_builtin_screensaver (gpointer unused)
{
        int current_server_timeout, current_server_interval;
        int current_prefer_blank,   current_allow_exp;
        int desired_server_timeout, desired_server_interval;
        int desired_prefer_blank,   desired_allow_exp;

        XGetScreenSaver (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                         &current_server_timeout,
                         &current_server_interval,
                         &current_prefer_blank,
                         &current_allow_exp);

        desired_server_timeout  = current_server_timeout;
        desired_server_interval = current_server_interval;
        desired_prefer_blank    = current_prefer_blank;
        desired_allow_exp       = current_allow_exp;

        desired_server_interval = 0;

        /* I suspect (but am not sure) that DontAllowExposures might have
           something to do with powering off the monitor as well, at least
           on some systems that don't support XDPMS?  Who know... */
        desired_allow_exp = AllowExposures;

        /* When we're not using an extension, set the server-side timeout to 0,
           so that the server never gets involved with screen blanking, and we
           do it all ourselves.  (However, when we *are* using an extension,
           we tell the server when to notify us, and rather than blanking the
           screen, the server will send us an X event telling us to blank.)
        */
        desired_server_timeout = 0;

        if (desired_server_timeout     != current_server_timeout
            || desired_server_interval != current_server_interval
            || desired_prefer_blank    != current_prefer_blank
            || desired_allow_exp       != current_allow_exp) {

                g_debug ("disabling server builtin screensaver:"
                         " (xset s %d %d; xset s %s; xset s %s)",
                         desired_server_timeout,
                         desired_server_interval,
                         (desired_prefer_blank ? "blank" : "noblank"),
                         (desired_allow_exp ? "expose" : "noexpose"));

                XSetScreenSaver (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                                 desired_server_timeout,
                                 desired_server_interval,
                                 desired_prefer_blank,
                                 desired_allow_exp);

                XSync (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), FALSE);
        }

        return TRUE;
}

guint
gsd_power_enable_screensaver_watchdog (void)
{
        int dummy;
        guint id;

        /* Make sure that Xorg's DPMS extension never gets in our
         * way. The defaults are now applied in Fedora 20 from
         * being "0" by default to being "600" by default */
        gdk_error_trap_push ();
        if (DPMSQueryExtension(GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), &dummy, &dummy))
                DPMSSetTimeouts (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), 0, 0, 0);
        gdk_error_trap_pop_ignored ();
        id = g_timeout_add_seconds (XSCREENSAVER_WATCHDOG_TIMEOUT,
                                    disable_builtin_screensaver,
                                    NULL);
        g_source_set_name_by_id (id, "[gnome-settings-daemon] disable_builtin_screensaver");
        return id;
}

static GnomeRROutput *
get_primary_output (GnomeRRScreen *rr_screen)
{
        GnomeRROutput *output = NULL;
        GnomeRROutput **outputs;
        guint i;

        /* search all X11 outputs for the device id */
        outputs = gnome_rr_screen_list_outputs (rr_screen);
        if (outputs == NULL)
                goto out;

        for (i = 0; outputs[i] != NULL; i++) {
                if (gnome_rr_output_is_builtin_display (outputs[i]) &&
                    gnome_rr_output_get_backlight (outputs[i]) >= 0) {
                        output = outputs[i];
                        break;
                }
        }
out:
        return output;
}

static gpointer
parse_mocked (gpointer data)
{
	const char *mocked;
	mocked = g_getenv ("GSD_MOCKED");
	if (!mocked)
		return GINT_TO_POINTER (FALSE);
	return GINT_TO_POINTER (TRUE);
}

static gboolean
is_mocked (void)
{
	  static GOnce mocked_once = G_ONCE_INIT;
	  g_once (&mocked_once, parse_mocked, NULL);
	  return GPOINTER_TO_INT (mocked_once.retval);
}

static void
backlight_set_mock_value (gint value)
{
	const char *filename;
	char *contents;
	GError *error = NULL;

	g_debug ("Setting mock brightness: %d", value);

	filename = "GSD_MOCK_brightness";
	contents = g_strdup_printf ("%d", value);
	if (!g_file_set_contents (filename, contents, -1, &error))
		g_warning ("Setting mock brightness failed: %s", error->message);
	g_clear_error (&error);
	g_free (contents);
}

static gint64
backlight_get_mock_value (enum BacklightHelperCommand command)
{
	char *contents;
	gint64 ret;

        if (command == BACKLIGHT_HELPER_GET_MAX) {
		g_debug ("Returning max mock brightness: %d", GSD_MOCK_MAX_BRIGHTNESS);
		return GSD_MOCK_MAX_BRIGHTNESS;
	}

        g_assert (command == BACKLIGHT_HELPER_GET);

	if (g_file_get_contents ("GSD_MOCK_brightness", &contents, NULL, NULL)) {
		ret = g_ascii_strtoll (contents, NULL, 0);
		g_free (contents);
		g_debug ("Returning mock brightness: %"G_GINT64_FORMAT, ret);
	} else {
		ret = GSD_MOCK_DEFAULT_BRIGHTNESS;
		backlight_set_mock_value (GSD_MOCK_DEFAULT_BRIGHTNESS);
		g_debug ("Returning default mock brightness: %"G_GINT64_FORMAT, ret);
	}

	return ret;
}

gboolean
backlight_available (GnomeRRScreen *rr_screen)
{
        char *path;

	if (is_mocked ())
		return TRUE;

#ifndef __linux__
        return (get_primary_output (rr_screen) != NULL);
#endif

        path = gsd_backlight_helper_get_best_backlight (NULL);
        if (path == NULL)
                return FALSE;

        g_free (path);
        return TRUE;
}

static gchar **
get_backlight_helper_environ (void)
{
        static gchar **environ = NULL;

        if (environ)
                return environ;

        environ = g_environ_unsetenv (g_get_environ (), "SHELL");
        return environ;
}

static gboolean
run_backlight_helper (enum BacklightHelperCommand   command,
                      gchar                        *value,
                      gchar                       **stdout_data,
                      gint                         *exit_status,
                      GError                      **error)
{
        static gchar *helper_args[] = {
                "--get-brightness",
                "--get-max-brightness",
                "--set-brightness",
        };
        gchar *argv[5] = { 0 };

        argv[0] = "pkexec";
        argv[1] = LIBEXECDIR "/gsd-backlight-helper";
        argv[2] = helper_args[command];
        argv[3] = value;

        return g_spawn_sync (NULL,
                             command == BACKLIGHT_HELPER_SET ? argv : &argv[1],
                             get_backlight_helper_environ (),
                             G_SPAWN_SEARCH_PATH,
                             NULL,
                             NULL,
                             stdout_data,
                             NULL,
                             exit_status,
                             error);
}

/**
 * backlight_helper_get_value:
 *
 * Gets a brightness value from the PolicyKit helper.
 *
 * Return value: the signed integer value from the helper, or -1
 * for failure. If -1 then @error is set.
 **/
static gint64
backlight_helper_get_value (enum BacklightHelperCommand command, GError **error)
{
        gboolean ret;
        gchar *stdout_data = NULL;
        gint exit_status = 0;
        gint64 value = -1;
        gchar *endptr = NULL;

	if (is_mocked ())
		return backlight_get_mock_value (command);

#ifndef __linux__
        /* non-Linux platforms won't have /sys/class/backlight */
        g_set_error_literal (error,
                             GSD_POWER_MANAGER_ERROR,
                             GSD_POWER_MANAGER_ERROR_FAILED,
                             "The sysfs backlight helper is only for Linux");
        goto out;
#endif

        /* get the data */
        ret = run_backlight_helper (command, NULL,
                                    &stdout_data, &exit_status, error);
        if (!ret)
                goto out;

        if (WEXITSTATUS (exit_status) != 0) {
                 g_set_error (error,
                             GSD_POWER_MANAGER_ERROR,
                             GSD_POWER_MANAGER_ERROR_FAILED,
                             "gsd-backlight-helper failed: %s",
                             stdout_data ? stdout_data : "No reason");
                goto out;
        }

        /* parse */
        value = g_ascii_strtoll (stdout_data, &endptr, 10);

        /* parsing error */
        if (endptr == stdout_data) {
                value = -1;
                g_set_error (error,
                             GSD_POWER_MANAGER_ERROR,
                             GSD_POWER_MANAGER_ERROR_FAILED,
                             "failed to parse value: %s",
                             stdout_data);
                goto out;
        }

        /* out of range */
        if (value > G_MAXINT) {
                value = -1;
                g_set_error (error,
                             GSD_POWER_MANAGER_ERROR,
                             GSD_POWER_MANAGER_ERROR_FAILED,
                             "value out of range: %s",
                             stdout_data);
                goto out;
        }

        /* Fetching the value failed, for some other reason */
        if (value < 0) {
                g_set_error (error,
                             GSD_POWER_MANAGER_ERROR,
                             GSD_POWER_MANAGER_ERROR_FAILED,
                             "value negative, but helper did not fail: %s",
                             stdout_data);
                goto out;
        }

out:
        g_free (stdout_data);
        return value;
}

/**
 * backlight_helper_set_value:
 *
 * Sets a brightness value using the PolicyKit helper.
 *
 * Return value: Success. If FALSE then @error is set.
 **/
static gboolean
backlight_helper_set_value (gint value,
                            GError **error)
{
        gboolean ret = FALSE;
        gint exit_status = 0;
        gchar *vstr = NULL;

	if (is_mocked ()) {
		backlight_set_mock_value (value);
		return TRUE;
	}

#ifndef __linux__
        /* non-Linux platforms won't have /sys/class/backlight */
        g_set_error_literal (error,
                             GSD_POWER_MANAGER_ERROR,
                             GSD_POWER_MANAGER_ERROR_FAILED,
                             "The sysfs backlight helper is only for Linux");
        return FALSE;
#endif

        /* get the data */
        vstr = g_strdup_printf ("%i", value);
        ret = run_backlight_helper (BACKLIGHT_HELPER_SET, vstr,
                                    NULL, &exit_status, error);
        g_free (vstr);
        return ret;
}

int
backlight_get_output_id (GnomeRRScreen *rr_screen)
{
        GnomeRROutput *output;
        GnomeRRCrtc *crtc;
        GdkScreen *gdk_screen;
        gint x, y;

        output = get_primary_output (rr_screen);
        if (output == NULL)
                return -1;

        crtc = gnome_rr_output_get_crtc (output);
        if (crtc == NULL)
                return -1;

        gdk_screen = gdk_screen_get_default ();
        gnome_rr_crtc_get_position (crtc, &x, &y);

        return gdk_screen_get_monitor_at_point (gdk_screen, x, y);
}

int
backlight_get_abs (GnomeRRScreen *rr_screen, GError **error)
{
#ifndef __linux__
        GnomeRROutput *output;
        output = get_primary_output (rr_screen);
        if (output != NULL) {
                return gnome_rr_output_get_backlight (output);
        }
        return -1;
#else
        return backlight_helper_get_value (BACKLIGHT_HELPER_GET, error);
#endif
}

int
backlight_get_percentage (GnomeRRScreen *rr_screen, GError **error)
{
        gint now;
        gint value = -1;
        gint max;
#ifndef __linux__
        GnomeRROutput *output;
        output = get_primary_output (rr_screen);
        if (output != NULL) {
                now = gnome_rr_output_get_backlight (output);
                if (now < 0)
                        return value;
                value = ABS_TO_PERCENTAGE (0, 100, now);
        }
        return value;
#else
        max = backlight_helper_get_value (BACKLIGHT_HELPER_GET_MAX, error);
        if (max < 0)
                return value;
        now = backlight_helper_get_value (BACKLIGHT_HELPER_GET, error);
        if (now < 0)
                return value;
        value = ABS_TO_PERCENTAGE (0, max, now);
        return value;
#endif
}

int
backlight_get_min (GnomeRRScreen *rr_screen)
{
        return 0;
}

int
backlight_get_max (GnomeRRScreen *rr_screen, GError **error)
{
#ifndef __linux__
        return 100;
#else
        return  backlight_helper_get_value (BACKLIGHT_HELPER_GET_MAX, error);
#endif
}

gboolean
backlight_set_percentage (GnomeRRScreen *rr_screen,
                          gint *value,
                          GError **error)
{
        gboolean ret = FALSE;
        gint max;
        guint discrete;
#ifndef __linux__
        GnomeRROutput *output;
        output = get_primary_output (rr_screen);
        if (output != NULL) {
                if (!gnome_rr_output_set_backlight (output, *value, error))
                        return ret;
                *value = gnome_rr_output_get_backlight (output);
                ret = TRUE;
        }
        return ret;
#else
        max = backlight_helper_get_value (BACKLIGHT_HELPER_GET_MAX, error);
        if (max < 0)
                return ret;
        discrete = PERCENTAGE_TO_ABS (0, max, *value);
        ret = backlight_helper_set_value (discrete, error);
        if (ret)
                *value = ABS_TO_PERCENTAGE (0, max, discrete);

        return ret;
#endif
}

int
backlight_step_up (GnomeRRScreen *rr_screen, GError **error)
{
        gboolean ret = FALSE;
        gint percentage_value = -1;
        gint max;
        gint now;
        gint step;
        guint discrete;
#ifndef __linux__
        GnomeRRCrtc *crtc;
        GnomeRROutput *output;
        output = get_primary_output (rr_screen);
        if (output != NULL) {

                crtc = gnome_rr_output_get_crtc (output);
                if (crtc == NULL) {
                        g_set_error (error,
                                     GSD_POWER_MANAGER_ERROR,
                                     GSD_POWER_MANAGER_ERROR_FAILED,
                                     "no crtc for %s",
                                     gnome_rr_output_get_name (output));
                        return percentage_value;
                }
                max = 100;
                now = gnome_rr_output_get_backlight (output);
                if (now < 0)
                       return percentage_value;
                step = MAX(gnome_rr_output_get_min_backlight_step (output), BRIGHTNESS_STEP_AMOUNT(max + 1));
                discrete = MIN (now + step, max);
                ret = gnome_rr_output_set_backlight (output,
                                                     discrete,
                                                     error);
                if (ret)
                        percentage_value = ABS_TO_PERCENTAGE (0, max, discrete);
        }
        return percentage_value;
#else
        now = backlight_helper_get_value (BACKLIGHT_HELPER_GET, error);
        if (now < 0)
                return percentage_value;
        max = backlight_helper_get_value (BACKLIGHT_HELPER_GET_MAX, error);
        if (max < 0)
                return percentage_value;
        step = BRIGHTNESS_STEP_AMOUNT (max + 1);
        discrete = MIN (now + step, max);
        ret = backlight_helper_set_value (discrete, error);
        if (ret)
                percentage_value = ABS_TO_PERCENTAGE (0, max, discrete);

        return percentage_value;
#endif
}

int
backlight_step_down (GnomeRRScreen *rr_screen, GError **error)
{
        gboolean ret = FALSE;
        gint percentage_value = -1;
        gint max;
        gint now;
        gint step;
        guint discrete;
#ifndef __linux__
        GnomeRRCrtc *crtc;
        GnomeRROutput *output;
        output = get_primary_output (rr_screen);
        if (output != NULL) {

                crtc = gnome_rr_output_get_crtc (output);
                if (crtc == NULL) {
                        g_set_error (error,
                                     GSD_POWER_MANAGER_ERROR,
                                     GSD_POWER_MANAGER_ERROR_FAILED,
                                     "no crtc for %s",
                                     gnome_rr_output_get_name (output));
                        return percentage_value;
                }
                max = 100;
                now = gnome_rr_output_get_backlight (output);
                if (now < 0)
                       return percentage_value;
                step = MAX (gnome_rr_output_get_min_backlight_step (output), BRIGHTNESS_STEP_AMOUNT (max + 1));
                discrete = MAX (now - step, 0);
                ret = gnome_rr_output_set_backlight (output,
                                                     discrete,
                                                     error);
                if (ret)
                        percentage_value = ABS_TO_PERCENTAGE (0, max, discrete);
        }
        return percentage_value;
#else
        now = backlight_helper_get_value (BACKLIGHT_HELPER_GET, error);
        if (now < 0)
                return percentage_value;
        max = backlight_helper_get_value (BACKLIGHT_HELPER_GET_MAX, error);
        if (max < 0)
                return percentage_value;
        step = BRIGHTNESS_STEP_AMOUNT (max + 1);
        discrete = MAX (now - step, 0);
        ret = backlight_helper_set_value (discrete, error);
        if (ret)
                percentage_value = ABS_TO_PERCENTAGE (0, max, discrete);

        return percentage_value;
#endif
}

int
backlight_set_abs (GnomeRRScreen *rr_screen,
                   guint value,
                   GError **error)
{
        gboolean ret = FALSE;
#ifndef __linux__
        GnomeRROutput *output;
        output = get_primary_output (rr_screen);
        if (output != NULL)
                return gnome_rr_output_set_backlight (output, value, error);
        return ret;
#else
        ret = backlight_helper_set_value (value, error);

        return ret;
#endif
}

void
reset_idletime (void)
{
        static KeyCode keycode;

        if (keycode == 0) {
                keycode = XKeysymToKeycode (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), GDK_KEY_WakeUp);
                if (keycode == 0)
                        keycode = XKeysymToKeycode (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), GDK_KEY_Alt_L);
        }

        gdk_error_trap_push ();
        /* send a wakeup or left alt key; first press, then release */
        XTestFakeKeyEvent (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), keycode, True, CurrentTime);
        XTestFakeKeyEvent (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), keycode, False, CurrentTime);
        gdk_error_trap_pop_ignored ();
}

static gboolean
randr_output_is_on (GnomeRROutput *output)
{
        GnomeRRCrtc *crtc;

        crtc = gnome_rr_output_get_crtc (output);
        if (!crtc)
                return FALSE;
        return gnome_rr_crtc_get_current_mode (crtc) != NULL;
}

static void
mock_monitor_changed (GFileMonitor     *monitor,
		      GFile            *file,
		      GFile            *other_file,
		      GFileMonitorEvent event_type,
		      gpointer          user_data)
{
	GnomeRRScreen *screen = (GnomeRRScreen *) user_data;

	g_debug ("Mock screen configuration changed");
	g_signal_emit_by_name (G_OBJECT (screen), "changed");
}

static void
screen_destroyed (gpointer  user_data,
		  GObject  *where_the_object_was)
{
	g_object_unref (G_OBJECT (user_data));
}

void
watch_external_monitor (GnomeRRScreen *screen)
{
	GFile *file;
	GFileMonitor *monitor;

	if (!is_mocked ())
		return;

	file = g_file_new_for_commandline_arg ("GSD_MOCK_EXTERNAL_MONITOR");
	monitor = g_file_monitor (file, G_FILE_MONITOR_NONE, NULL, NULL);
	g_object_unref (file);
	g_signal_connect (monitor, "changed",
			  G_CALLBACK (mock_monitor_changed), screen);
	g_object_weak_ref (G_OBJECT (screen), screen_destroyed, monitor);
}

static gboolean
mock_external_monitor_is_connected (GnomeRRScreen *screen)
{
	char *mock_external_monitor_contents;

	if (g_file_get_contents ("GSD_MOCK_EXTERNAL_MONITOR", &mock_external_monitor_contents, NULL, NULL)) {
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
external_monitor_is_connected (GnomeRRScreen *screen)
{
        GnomeRROutput **outputs;
        guint i;

        if (is_mocked ())
                return mock_external_monitor_is_connected (screen);

	g_assert (screen != NULL);

        /* see if we have more than one screen plugged in */
        outputs = gnome_rr_screen_list_outputs (screen);
        for (i = 0; outputs[i] != NULL; i++) {
                if (randr_output_is_on (outputs[i]) &&
                    !gnome_rr_output_is_builtin_display (outputs[i]))
                        return TRUE;
        }

        return FALSE;
}

static void
play_sound (void)
{
        ca_context_play (ca_gtk_context_get (), UPS_SOUND_LOOP_ID,
                         CA_PROP_EVENT_ID, "battery-caution",
                         CA_PROP_EVENT_DESCRIPTION, _("Battery is critically low"), NULL);
}

static gboolean
play_loop_timeout_cb (gpointer user_data)
{
        play_sound ();
        return TRUE;
}

void
play_loop_start (guint *id)
{
        if (*id != 0)
                return;

        *id = g_timeout_add_seconds (GSD_POWER_MANAGER_CRITICAL_ALERT_TIMEOUT,
                                     (GSourceFunc) play_loop_timeout_cb,
                                     NULL);
        g_source_set_name_by_id (*id, "[gnome-settings-daemon] play_loop_timeout_cb");
        play_sound ();
}

void
play_loop_stop (guint *id)
{
        if (*id == 0)
                return;

        ca_context_cancel (ca_gtk_context_get (), UPS_SOUND_LOOP_ID);
        g_source_remove (*id);
        *id = 0;
}
