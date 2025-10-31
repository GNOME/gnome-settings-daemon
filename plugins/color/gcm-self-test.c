/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2011 Richard Hughes <richard@hughsie.com>
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

#include <gdesktop-enums.h>
#include <glib.h>
#include <glib-object.h>
#include <stdlib.h>

#include "gsd-color-state.h"
#include "gsd-color-scheme.h"
#include "gsd-location-monitor.h"
#include "gsd-night-light.h"
#include "gsd-night-light-common.h"

GMainLoop *mainloop;

static void
on_notify (GsdLocationMonitor *monitor,
           GParamSpec *pspec,
           gpointer    user_data)
{
        guint *cnt = (guint *) user_data;
        (*cnt)++;
}

static gboolean
quit_mainloop (gpointer user_data)
{
    g_main_loop_quit (mainloop);

    return FALSE;
}

static void
gcm_test_location_monitor (void)
{
        gboolean ret;
        guint sunrise_cnt = 0;
        guint sunset_cnt = 0;
        g_autoptr(GDateTime) datetime_override = NULL;
        g_autoptr(GError) error = NULL;
        g_autoptr(GsdLocationMonitor) monitor = NULL;
        g_autoptr(GSettings) settings = NULL;

        monitor = gsd_location_monitor_get ();
        g_assert (GSD_IS_LOCATION_MONITOR (monitor));
        g_signal_connect (monitor, "notify::sunset",
                          G_CALLBACK (on_notify), &sunset_cnt);
        g_signal_connect (monitor, "notify::sunrise",
                          G_CALLBACK (on_notify), &sunrise_cnt);

        /* hardcode a specific date and time */
        datetime_override = g_date_time_new_utc (2017, 2, 8, 20, 0, 0);
        gsd_location_monitor_set_date_time_now (monitor, datetime_override);

        /* do not start geoclue */
        gsd_location_monitor_set_geoclue_enabled (monitor, FALSE);

        settings = g_settings_new ("org.gnome.settings-daemon.plugins.color");

        /* check default values */
        g_assert_cmpint ((gint) gsd_location_monitor_get_sunrise (monitor), ==, -1);
        g_assert_cmpint ((gint) gsd_location_monitor_get_sunset (monitor), ==, -1);

        /* start module, disabled */
        ret = gsd_location_monitor_start (monitor, &error);
        g_assert_no_error (error);
        g_assert (ret);
        g_assert_cmpint (sunset_cnt, ==, 0);
        g_assert_cmpint (sunrise_cnt, ==, 0);

        g_settings_set_value (settings, "night-light-last-coordinates",
                              g_variant_new ("(dd)", 51.5, -0.1278));
        g_assert_cmpint (sunset_cnt, ==, 1);
        g_assert_cmpint (sunrise_cnt, ==, 1);
        g_assert_cmpint ((gint) gsd_location_monitor_get_sunrise (monitor), ==, 7);
        g_assert_cmpint ((gint) gsd_location_monitor_get_sunset (monitor), ==, 17);

        /* reset coordinates */
        g_settings_reset (settings, "night-light-last-coordinates");
}

static void
gcm_test_color_scheme (void)
{
        gboolean ret;
        guint sunrise_cnt = 0;
        guint sunset_cnt = 0;
        g_autoptr(GDateTime) datetime_override = NULL;
        g_autoptr(GError) error = NULL;
        g_autoptr(GsdColorScheme) color_scheme = NULL;
        g_autoptr(GsdLocationMonitor) monitor = NULL;
        g_autoptr(GSettings) settings = NULL;
        g_autoptr(GSettings) scheme_settings = NULL;

        monitor = gsd_location_monitor_get ();
        g_assert (GSD_IS_LOCATION_MONITOR (monitor));
        g_signal_connect (monitor, "notify::sunset",
                          G_CALLBACK (on_notify), &sunset_cnt);
        g_signal_connect (monitor, "notify::sunrise",
                          G_CALLBACK (on_notify), &sunrise_cnt);
        
        color_scheme = gsd_color_scheme_new ();
        g_assert (GSD_IS_COLOR_SCHEME (color_scheme));

        /* hardcode a specific date and time */
        datetime_override = g_date_time_new_utc (2017, 2, 8, 20, 0, 0);
        gsd_location_monitor_set_date_time_now (monitor, datetime_override);
        gsd_color_scheme_recheck_immediate (color_scheme);

        /* do not start geoclue */
        gsd_location_monitor_set_geoclue_enabled (monitor, FALSE);

        /* switch off */
        settings = g_settings_new ("org.gnome.settings-daemon.plugins.color");
        scheme_settings = g_settings_new ("org.gnome.desktop.interface");
        g_settings_set_boolean (settings, "color-scheme-enabled", FALSE);
        g_settings_set_enum (scheme_settings, "color-scheme", G_DESKTOP_COLOR_SCHEME_PREFER_DARK);

        /* start module, disabled */
        ret = gsd_location_monitor_start (monitor, &error);
        g_assert_no_error (error);
        g_assert (ret);
        ret = gsd_color_scheme_start (color_scheme, &error);
        g_assert_no_error (error);
        g_assert (ret);
        g_assert_cmpint (sunset_cnt, ==, 0);
        g_assert_cmpint (sunrise_cnt, ==, 0);

        /* enable automatic mode */
        g_settings_set_value (settings, "night-light-last-coordinates",
                              g_variant_new ("(dd)", 51.5, -0.1278));
        g_settings_set_boolean (settings, "color-scheme-schedule-automatic", TRUE);
        g_settings_set_boolean (settings, "color-scheme-enabled", TRUE);
        g_assert_cmpint (sunset_cnt, ==, 1);
        g_assert_cmpint (sunrise_cnt, ==, 1);
        g_assert_cmpint ((gint) gsd_location_monitor_get_sunrise (monitor), ==, 7);
        g_assert_cmpint ((gint) gsd_location_monitor_get_sunset (monitor), ==, 17);
        g_assert_cmpint ((gint) g_settings_get_enum (scheme_settings, "color-scheme"), ==, 1); // Dark

        /* enabled manual mode */
        g_settings_set_double (settings, "color-scheme-schedule-from", 4.0);
        g_settings_set_double (settings, "color-scheme-schedule-to", 16.f);
        g_settings_set_boolean (settings, "color-scheme-schedule-automatic", FALSE);
        g_assert_cmpint (sunset_cnt, ==, 1);
        g_assert_cmpint (sunrise_cnt, ==, 1);
        g_assert_cmpint ((gint) gsd_location_monitor_get_sunrise (monitor), ==, 7);
        g_assert_cmpint ((gint) gsd_location_monitor_get_sunset (monitor), ==, 17);
        g_assert_cmpint ((gint) g_settings_get_enum (scheme_settings, "color-scheme"), ==, 0); // Light

        /* disable, with no changes */
        g_settings_set_boolean (settings, "color-scheme-enabled", FALSE);
        g_assert_cmpint (sunset_cnt, ==, 1);
        g_assert_cmpint (sunrise_cnt, ==, 1);
        g_assert_cmpint ((gint) g_settings_get_enum (scheme_settings, "color-scheme"), ==, 0); // Light

        g_clear_pointer (&datetime_override, g_date_time_unref);
        datetime_override = g_date_time_new_utc (2017, 2, 9, 5, 0, 0);
        gsd_location_monitor_set_date_time_now (monitor, datetime_override);
        gsd_color_scheme_recheck_immediate (color_scheme);
        g_assert_cmpint ((gint) g_settings_get_enum (scheme_settings, "color-scheme"), ==, 0); // Light

        /* enable and adjust schedule */
        g_settings_set_boolean (settings, "color-scheme-enabled", TRUE);

        /* Now, sleep for a bit (we need some time to actually update) */
        g_timeout_add (1000, quit_mainloop, NULL);
        g_main_loop_run (mainloop);

        g_assert_cmpint ((gint) g_settings_get_enum (scheme_settings, "color-scheme"), ==, 1); // Dark

        /* adjust schedule again */
        g_clear_pointer (&datetime_override, g_date_time_unref);
        datetime_override = g_date_time_new_utc (2017, 2, 8, 10, 00, 00);
        gsd_location_monitor_set_date_time_now (monitor, datetime_override);
        gsd_color_scheme_recheck_immediate (color_scheme);
        g_timeout_add (1000, quit_mainloop, NULL);
        g_main_loop_run (mainloop);
        g_assert_cmpint ((gint) g_settings_get_enum (scheme_settings, "color-scheme"), ==, 1); // Dark

        /* Check that we are always dark if from/to are equal */
        g_settings_set_double (settings, "color-scheme-schedule-from", 6.0);
        g_settings_set_double (settings, "color-scheme-schedule-to", 6.0);

        datetime_override = g_date_time_new_utc (2017, 2, 8, 5, 50, 0);
        gsd_location_monitor_set_date_time_now (monitor, datetime_override);
        gsd_color_scheme_recheck_immediate (color_scheme);
        g_assert_cmpint ((gint) g_settings_get_enum (scheme_settings, "color-scheme"), ==, 1); // Dark

        datetime_override = g_date_time_new_utc (2017, 2, 8, 6, 0, 0);
        gsd_location_monitor_set_date_time_now (monitor, datetime_override);
        gsd_color_scheme_recheck_immediate (color_scheme);
        g_assert_cmpint ((gint) g_settings_get_enum (scheme_settings, "color-scheme"), ==, 1); // Dark

        datetime_override = g_date_time_new_utc (2017, 2, 8, 6, 10, 0);
        gsd_location_monitor_set_date_time_now (monitor, datetime_override);
        gsd_color_scheme_recheck_immediate (color_scheme);
        g_assert_cmpint ((gint) g_settings_get_enum (scheme_settings, "color-scheme"), ==, 1); // Dark

        /* reset coordinates */
        g_settings_reset (settings, "night-light-last-coordinates");
}

static void
gcm_test_night_light (void)
{
        gboolean ret;
        guint active_cnt = 0;
        guint disabled_until_tmw_cnt = 0;
        guint sunrise_cnt = 0;
        guint sunset_cnt = 0;
        guint temperature_cnt = 0;
        g_autoptr(GDateTime) datetime_override = NULL;
        g_autoptr(GError) error = NULL;
        g_autoptr(GsdNightLight) nlight = NULL;
        g_autoptr(GsdLocationMonitor) monitor = NULL;
        g_autoptr(GSettings) settings = NULL;

        monitor = gsd_location_monitor_get ();
        g_assert (GSD_IS_LOCATION_MONITOR (monitor));
        g_signal_connect (monitor, "notify::sunset",
                          G_CALLBACK (on_notify), &sunset_cnt);
        g_signal_connect (monitor, "notify::sunrise",
                          G_CALLBACK (on_notify), &sunrise_cnt);

        nlight = gsd_night_light_new ();
        g_assert (GSD_IS_NIGHT_LIGHT (nlight));
        g_signal_connect (nlight, "notify::active",
                          G_CALLBACK (on_notify), &active_cnt);
        g_signal_connect (nlight, "notify::temperature",
                          G_CALLBACK (on_notify), &temperature_cnt);
        g_signal_connect (nlight, "notify::disabled-until-tmw",
                          G_CALLBACK (on_notify), &disabled_until_tmw_cnt);

        /* hardcode a specific date and time */
        datetime_override = g_date_time_new_utc (2017, 2, 8, 20, 0, 0);
        gsd_location_monitor_set_date_time_now (monitor, datetime_override);
        gsd_night_light_recheck_immediate (nlight);

        /* do not start geoclue */
        gsd_location_monitor_set_geoclue_enabled (monitor, FALSE);

        /* do not smooth the transition */
        gsd_night_light_set_smooth_enabled (nlight, FALSE);

        /* switch off */
        settings = g_settings_new ("org.gnome.settings-daemon.plugins.color");
        g_settings_set_boolean (settings, "night-light-enabled", FALSE);
        g_settings_set_uint (settings, "night-light-temperature", 4000);

        /* check default values */
        g_assert (!gsd_night_light_get_active (nlight));
        g_assert_cmpint ((gint) gsd_location_monitor_get_sunrise (monitor), ==, -1);
        g_assert_cmpint ((gint) gsd_location_monitor_get_sunset (monitor), ==, -1);

        g_assert_cmpint (gsd_night_light_get_temperature (nlight), ==, GSD_COLOR_TEMPERATURE_DEFAULT);
        g_assert (!gsd_night_light_get_disabled_until_tmw (nlight));

        /* start module, disabled */
        ret = gsd_location_monitor_start (monitor, &error);
        g_assert_no_error (error);
        g_assert (ret);
        ret = gsd_night_light_start (nlight, &error);
        g_assert_no_error (error);
        g_assert (ret);
        g_assert (!gsd_night_light_get_active (nlight));
        g_assert_cmpint (active_cnt, ==, 0);
        g_assert_cmpint (sunset_cnt, ==, 0);
        g_assert_cmpint (sunrise_cnt, ==, 0);
        g_assert_cmpint (temperature_cnt, ==, 0);
        g_assert_cmpint (disabled_until_tmw_cnt, ==, 0);

        /* enable automatic mode */
        g_settings_set_value (settings, "night-light-last-coordinates",
                              g_variant_new ("(dd)", 51.5, -0.1278));
                              
        g_settings_set_boolean (settings, "night-light-schedule-automatic", TRUE);
        g_settings_set_boolean (settings, "night-light-enabled", TRUE);
        g_assert (gsd_night_light_get_active (nlight));
        g_assert_cmpint (active_cnt, ==, 1);
        g_assert_cmpint (sunset_cnt, ==, 1);
        g_assert_cmpint (sunrise_cnt, ==, 1);
        g_assert_cmpint (temperature_cnt, ==, 1);
        g_assert_cmpint (disabled_until_tmw_cnt, ==, 0);
        g_assert_cmpint ((gint) gsd_location_monitor_get_sunrise (monitor), ==, 7);
        g_assert_cmpint ((gint) gsd_location_monitor_get_sunset (monitor), ==, 17);
        g_assert_cmpint (gsd_night_light_get_temperature (nlight), ==, 4000);
        g_assert (!gsd_night_light_get_disabled_until_tmw (nlight));

        /* disable for one day */
        gsd_night_light_set_disabled_until_tmw (nlight, TRUE);
        gsd_night_light_set_disabled_until_tmw (nlight, TRUE);
        g_assert_cmpint (gsd_night_light_get_temperature (nlight), ==, GSD_COLOR_TEMPERATURE_DEFAULT);
        g_assert (gsd_night_light_get_active (nlight));
        g_assert (gsd_night_light_get_disabled_until_tmw (nlight));
        g_assert_cmpint (temperature_cnt, ==, 2);
        g_assert_cmpint (disabled_until_tmw_cnt, ==, 1);

        /* change our mind */
        gsd_night_light_set_disabled_until_tmw (nlight, FALSE);
        g_assert_cmpint (gsd_night_light_get_temperature (nlight), ==, 4000);
        g_assert (gsd_night_light_get_active (nlight));
        g_assert (!gsd_night_light_get_disabled_until_tmw (nlight));
        g_assert_cmpint (active_cnt, ==, 1);
        g_assert_cmpint (temperature_cnt, ==, 3);
        g_assert_cmpint (disabled_until_tmw_cnt, ==, 2);

        /* enabled manual mode (night shift) */
        g_settings_set_double (settings, "night-light-schedule-from", 4.0);
        g_settings_set_double (settings, "night-light-schedule-to", 16.f);
        g_settings_set_boolean (settings, "night-light-schedule-automatic", FALSE);
        g_assert_cmpint (active_cnt, ==, 2);
        g_assert_cmpint (sunset_cnt, ==, 1);
        g_assert_cmpint (sunrise_cnt, ==, 1);
        g_assert_cmpint (temperature_cnt, ==, 4);
        g_assert_cmpint (disabled_until_tmw_cnt, ==, 2);
        g_assert (!gsd_night_light_get_active (nlight));
        g_assert_cmpint ((gint) gsd_location_monitor_get_sunrise (monitor), ==, 7);
        g_assert_cmpint ((gint) gsd_location_monitor_get_sunset (monitor), ==, 17);
        g_assert_cmpint (gsd_night_light_get_temperature (nlight), ==, GSD_COLOR_TEMPERATURE_DEFAULT);
        g_assert (!gsd_night_light_get_disabled_until_tmw (nlight));

        /* disable, with no changes */
        g_settings_set_boolean (settings, "night-light-enabled", FALSE);
        g_assert (!gsd_night_light_get_active (nlight));
        g_assert_cmpint (active_cnt, ==, 2);
        g_assert_cmpint (sunset_cnt, ==, 1);
        g_assert_cmpint (sunrise_cnt, ==, 1);
        g_assert_cmpint (temperature_cnt, ==, 4);
        g_assert_cmpint (disabled_until_tmw_cnt, ==, 2);


        /* Finally, check that cancelling a smooth transition works */
        gsd_night_light_set_smooth_enabled (nlight, TRUE);
        /* Enable night light and automatic scheduling */
        g_settings_set_boolean (settings, "night-light-schedule-automatic", TRUE);
        g_settings_set_boolean (settings, "night-light-enabled", TRUE);
        /* It should be active again, and a smooth transition is being done,
         * so the color temperature is still the default at this point. */
        g_assert (gsd_night_light_get_active (nlight));
        g_assert_cmpint (gsd_night_light_get_temperature (nlight), ==, GSD_COLOR_TEMPERATURE_DEFAULT);

        /* Turn off immediately, before the first timeout event is fired. */
        g_settings_set_boolean (settings, "night-light-schedule-automatic", FALSE);
        g_settings_set_boolean (settings, "night-light-enabled", FALSE);
        g_assert (!gsd_night_light_get_active (nlight));

        /* Now, sleep for a bit (the smooth transition time is 5 seconds) */
        g_timeout_add (5000, quit_mainloop, NULL);
        g_main_loop_run (mainloop);

        /* Ensure that the color temperature is still the default one.*/
        g_assert_cmpint (gsd_night_light_get_temperature (nlight), ==, GSD_COLOR_TEMPERATURE_DEFAULT);


        /* Check that disabled until tomorrow resets again correctly. */
        g_settings_set_double (settings, "night-light-schedule-from", 17.0);
        g_settings_set_double (settings, "night-light-schedule-to", 7.f);
        g_settings_set_boolean (settings, "night-light-enabled", TRUE);
        gsd_night_light_set_disabled_until_tmw (nlight, TRUE);

        /* Move time past midnight */
        g_clear_pointer (&datetime_override, g_date_time_unref);
        datetime_override = g_date_time_new_utc (2017, 2, 9, 1, 0, 0);
        gsd_location_monitor_set_date_time_now (monitor, datetime_override);
        gsd_night_light_recheck_immediate (nlight);
        g_assert_true (gsd_night_light_get_disabled_until_tmw (nlight));

        /* Move past sunrise */
        g_clear_pointer (&datetime_override, g_date_time_unref);
        datetime_override = g_date_time_new_utc (2017, 2, 9, 8, 0, 0);
        gsd_location_monitor_set_date_time_now (monitor, datetime_override);
        gsd_night_light_recheck_immediate (nlight);
        g_assert_false (gsd_night_light_get_disabled_until_tmw (nlight));

        gsd_night_light_set_disabled_until_tmw (nlight, TRUE);

        /* Move into night more than 24h in the future */
        g_clear_pointer (&datetime_override, g_date_time_unref);
        datetime_override = g_date_time_new_utc (2017, 2, 10, 20, 0, 0);
        gsd_location_monitor_set_date_time_now (monitor, datetime_override);
        gsd_night_light_recheck_immediate (nlight);
        g_assert_false (gsd_night_light_get_disabled_until_tmw (nlight));


        /* Check that we are always in night mode if from/to are equal. */
        gsd_night_light_set_smooth_enabled (nlight, FALSE);
        g_settings_set_double (settings, "night-light-schedule-from", 6.0);
        g_settings_set_double (settings, "night-light-schedule-to", 6.0);
        g_settings_set_boolean (settings, "night-light-enabled", TRUE);

        datetime_override = g_date_time_new_utc (2017, 2, 10, 5, 50, 0);
        gsd_location_monitor_set_date_time_now (monitor, datetime_override);
        gsd_night_light_recheck_immediate (nlight);
        g_assert (gsd_night_light_get_active (nlight));
        g_assert_cmpint (gsd_night_light_get_temperature (nlight), ==, 4000);

        datetime_override = g_date_time_new_utc (2017, 2, 10, 6, 0, 0);
        gsd_location_monitor_set_date_time_now (monitor, datetime_override);
        gsd_night_light_recheck_immediate (nlight);
        g_assert (gsd_night_light_get_active (nlight));
        g_assert_cmpint (gsd_night_light_get_temperature (nlight), ==, 4000);

        datetime_override = g_date_time_new_utc (2017, 2, 10, 6, 10, 0);
        gsd_location_monitor_set_date_time_now (monitor, datetime_override);
        gsd_night_light_recheck_immediate (nlight);
        g_assert (gsd_night_light_get_active (nlight));
        g_assert_cmpint (gsd_night_light_get_temperature (nlight), ==, 4000);


        /* Check that the smearing time is lowered correctly when the times are close. */
        g_settings_set_double (settings, "night-light-schedule-from", 6.0);
        g_settings_set_double (settings, "night-light-schedule-to", 6.1);

        /* Not enabled 10 minutes before sunset */
        datetime_override = g_date_time_new_utc (2017, 2, 10, 5, 50, 0);
        gsd_location_monitor_set_date_time_now (monitor, datetime_override);
        gsd_night_light_recheck_immediate (nlight);
        g_assert_false (gsd_night_light_get_active (nlight));
        g_assert_cmpint (gsd_night_light_get_temperature (nlight), ==, GSD_COLOR_TEMPERATURE_DEFAULT);

        /* Not enabled >10 minutes after sunrise */
        datetime_override = g_date_time_new_utc (2017, 2, 10, 6, 20, 0);
        gsd_location_monitor_set_date_time_now (monitor, datetime_override);
        gsd_night_light_recheck_immediate (nlight);
        g_assert_false (gsd_night_light_get_active (nlight));
        g_assert_cmpint (gsd_night_light_get_temperature (nlight), ==, GSD_COLOR_TEMPERATURE_DEFAULT);

        /* ~50% smeared 3 min before sunrise (sunrise at 6 past) */
        datetime_override = g_date_time_new_utc (2017, 2, 10, 6, 3, 0);
        gsd_location_monitor_set_date_time_now (monitor, datetime_override);
        gsd_night_light_recheck_immediate (nlight);
        g_assert_true (gsd_night_light_get_active (nlight));
        g_assert_cmpint (gsd_night_light_get_temperature (nlight), <=, (GSD_COLOR_TEMPERATURE_DEFAULT + 4000) / 2 + 20);
        g_assert_cmpint (gsd_night_light_get_temperature (nlight), >=, (GSD_COLOR_TEMPERATURE_DEFAULT + 4000) / 2 - 20);

        /* ~50% smeared 3 min before sunset (sunset at 6 past) */
        g_settings_set_double (settings, "night-light-schedule-from", 6.1);
        g_settings_set_double (settings, "night-light-schedule-to", 6.0);
        datetime_override = g_date_time_new_utc (2017, 2, 10, 6, 3, 0);
        gsd_location_monitor_set_date_time_now (monitor, datetime_override);
        gsd_night_light_recheck_immediate (nlight);
        g_assert_true (gsd_night_light_get_active (nlight));
        g_assert_cmpint (gsd_night_light_get_temperature (nlight), <=, (GSD_COLOR_TEMPERATURE_DEFAULT + 4000) / 2 + 20);
        g_assert_cmpint (gsd_night_light_get_temperature (nlight), >=, (GSD_COLOR_TEMPERATURE_DEFAULT + 4000) / 2 - 20);

        /* reset coordinates */
        g_settings_reset (settings, "night-light-last-coordinates");
}

static void
gcm_test_sunset_sunrise (void)
{
        gdouble sunrise;
        gdouble sunrise_actual = 7.6;
        gdouble sunset;
        gdouble sunset_actual = 16.8;
        g_autoptr(GDateTime) dt = g_date_time_new_utc (2007, 2, 1, 0, 0, 0);

        /* get for London, today */
        gsd_night_light_get_sunrise_sunset (dt, 51.5, -0.1278, &sunrise, &sunset);
        g_assert_cmpfloat (sunrise, <, sunrise_actual + 0.1);
        g_assert_cmpfloat (sunrise, >, sunrise_actual - 0.1);
        g_assert_cmpfloat (sunset, <, sunset_actual + 0.1);
        g_assert_cmpfloat (sunset, >, sunset_actual - 0.1);
}

static void
gcm_test_sunset_sunrise_fractional_timezone (void)
{
        gdouble sunrise;
        gdouble sunrise_actual = 7.6 + 1.5;
        gdouble sunset;
        gdouble sunset_actual = 16.8 + 1.5;
        g_autoptr(GTimeZone) tz = NULL;
        g_autoptr(GDateTime) dt = NULL;

        tz = g_time_zone_new_identifier ("+01:30");
        g_assert_nonnull (tz);
        dt = g_date_time_new (tz, 2007, 2, 1, 0, 0, 0);

        /* get for our made up timezone, today */
        gsd_night_light_get_sunrise_sunset (dt, 51.5, -0.1278, &sunrise, &sunset);
        g_assert_cmpfloat (sunrise, <, sunrise_actual + 0.1);
        g_assert_cmpfloat (sunrise, >, sunrise_actual - 0.1);
        g_assert_cmpfloat (sunset, <, sunset_actual + 0.1);
        g_assert_cmpfloat (sunset, >, sunset_actual - 0.1);
}

static void
gcm_test_frac_day (void)
{
        g_autoptr(GDateTime) dt = g_date_time_new_utc (2007, 2, 1, 12, 59, 59);
        gdouble fd;
        gdouble fd_actual = 12.99;

        /* test for 12:59:59 */
        fd = gsd_night_light_frac_day_from_dt (dt);
        g_assert_cmpfloat (fd, >, fd_actual - 0.01);
        g_assert_cmpfloat (fd, <, fd_actual + 0.01);

        /* test same day */
        g_assert_true (gsd_night_light_frac_day_is_between (12, 6, 20));
        g_assert_false (gsd_night_light_frac_day_is_between (5, 6, 20));
        g_assert_true (gsd_night_light_frac_day_is_between (12, 0, 24));
        g_assert_true (gsd_night_light_frac_day_is_between (12, -1, 25));

        /* test rollover to next day */
        g_assert_true (gsd_night_light_frac_day_is_between (23, 20, 6));
        g_assert_false (gsd_night_light_frac_day_is_between (12, 20, 6));

        /* test rollover to the previous day */
        g_assert_true (gsd_night_light_frac_day_is_between (5, 16, 8));

        /* test equality */
        g_assert_true (gsd_night_light_frac_day_is_between (12, 0.5, 24.5));
        g_assert_true (gsd_night_light_frac_day_is_between (0.5, 0.5, 0.5));
}

int
main (int argc, char **argv)
{
        g_setenv ("GSETTINGS_BACKEND", "memory", TRUE);

        g_test_init (&argc, &argv, NULL);

        mainloop = g_main_loop_new (g_main_context_default (), FALSE);

        g_test_add_func ("/color/sunset-sunrise", gcm_test_sunset_sunrise);
        g_test_add_func ("/color/sunset-sunrise/fractional-timezone", gcm_test_sunset_sunrise_fractional_timezone);
        g_test_add_func ("/color/fractional-day", gcm_test_frac_day);
        g_test_add_func ("/color/location-monitor", gcm_test_location_monitor);
        g_test_add_func ("/color/night-light", gcm_test_night_light);
        g_test_add_func ("/color/color-scheme", gcm_test_color_scheme);

        return g_test_run ();
}

