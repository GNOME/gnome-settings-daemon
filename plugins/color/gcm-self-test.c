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

#include <glib-object.h>
#include <stdlib.h>
#include <gtk/gtk.h>

#include "gcm-edid.h"
#include "gsd-color-state.h"
#include "gsd-night-light.h"
#include "gsd-night-light-common.h"

static void
on_notify (GsdNightLight *nlight,
           GParamSpec      *pspec,
           gpointer         user_data)
{
        guint *cnt = (guint *) user_data;
        (*cnt)++;
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
        g_autoptr(GSettings) settings = NULL;

        nlight = gsd_night_light_new ();
        g_assert (GSD_IS_NIGHT_LIGHT (nlight));
        g_signal_connect (nlight, "notify::active",
                          G_CALLBACK (on_notify), &active_cnt);
        g_signal_connect (nlight, "notify::sunset",
                          G_CALLBACK (on_notify), &sunset_cnt);
        g_signal_connect (nlight, "notify::sunrise",
                          G_CALLBACK (on_notify), &sunrise_cnt);
        g_signal_connect (nlight, "notify::temperature",
                          G_CALLBACK (on_notify), &temperature_cnt);
        g_signal_connect (nlight, "notify::disabled-until-tmw",
                          G_CALLBACK (on_notify), &disabled_until_tmw_cnt);

        /* hardcode a specific date and time */
        datetime_override = g_date_time_new_utc (2017, 2, 8, 20, 0, 0);
        gsd_night_light_set_date_time_now (nlight, datetime_override);

        /* do not start geoclue */
        gsd_night_light_set_geoclue_enabled (nlight, FALSE);

        /* do not smooth the transition */
        gsd_night_light_set_smooth_enabled (nlight, FALSE);

        /* switch off */
        settings = g_settings_new ("org.gnome.settings-daemon.plugins.color");
        g_settings_set_boolean (settings, "night-light-enabled", FALSE);

        /* check default values */
        g_assert (!gsd_night_light_get_active (nlight));
        g_assert_cmpint ((gint) gsd_night_light_get_sunrise (nlight), ==, -1);
        g_assert_cmpint ((gint) gsd_night_light_get_sunset (nlight), ==, -1);
        g_assert_cmpint (gsd_night_light_get_temperature (nlight), ==, GSD_COLOR_TEMPERATURE_DEFAULT);
        g_assert (!gsd_night_light_get_disabled_until_tmw (nlight));

        /* start module, disabled */
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
        g_assert_cmpint ((gint) gsd_night_light_get_sunrise (nlight), ==, 7);
        g_assert_cmpint ((gint) gsd_night_light_get_sunset (nlight), ==, 17);
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
        g_assert_cmpint ((gint) gsd_night_light_get_sunrise (nlight), ==, 7);
        g_assert_cmpint ((gint) gsd_night_light_get_sunset (nlight), ==, 17);
        g_assert_cmpint (gsd_night_light_get_temperature (nlight), ==, GSD_COLOR_TEMPERATURE_DEFAULT);
        g_assert (!gsd_night_light_get_disabled_until_tmw (nlight));

        /* finally disable, with no changes */
        g_settings_set_boolean (settings, "night-light-enabled", FALSE);
        g_assert (!gsd_night_light_get_active (nlight));
        g_assert_cmpint (active_cnt, ==, 2);
        g_assert_cmpint (sunset_cnt, ==, 1);
        g_assert_cmpint (sunrise_cnt, ==, 1);
        g_assert_cmpint (temperature_cnt, ==, 4);
        g_assert_cmpint (disabled_until_tmw_cnt, ==, 2);
}

static void
gcm_test_edid_func (void)
{
        GcmEdid *edid;
        gchar *data;
        gboolean ret;
        GError *error = NULL;
        gsize length = 0;

        edid = gcm_edid_new ();
        g_assert (edid != NULL);

        /* LG 21" LCD panel */
        ret = g_file_get_contents (TESTDATADIR "/LG-L225W-External.bin",
                                   &data, &length, &error);
        g_assert_no_error (error);
        g_assert (ret);
        ret = gcm_edid_parse (edid, (const guint8 *) data, length, &error);
        g_assert_no_error (error);
        g_assert (ret);

        g_assert_cmpstr (gcm_edid_get_monitor_name (edid), ==, "L225W");
        g_assert_cmpstr (gcm_edid_get_vendor_name (edid), ==, "Goldstar Company Ltd");
        g_assert_cmpstr (gcm_edid_get_serial_number (edid), ==, "34398");
        g_assert_cmpstr (gcm_edid_get_eisa_id (edid), ==, NULL);
        g_assert_cmpstr (gcm_edid_get_checksum (edid), ==, "0bb44865bb29984a4bae620656c31368");
        g_assert_cmpstr (gcm_edid_get_pnp_id (edid), ==, "GSM");
        g_assert_cmpint (gcm_edid_get_height (edid), ==, 30);
        g_assert_cmpint (gcm_edid_get_width (edid), ==, 47);
        g_assert_cmpfloat (gcm_edid_get_gamma (edid), >=, 2.2f - 0.01);
        g_assert_cmpfloat (gcm_edid_get_gamma (edid), <, 2.2f + 0.01);
        g_free (data);

        /* Lenovo T61 internal Panel */
        ret = g_file_get_contents (TESTDATADIR "/Lenovo-T61-Internal.bin",
                                   &data, &length, &error);
        g_assert_no_error (error);
        g_assert (ret);
        ret = gcm_edid_parse (edid, (const guint8 *) data, length, &error);
        g_assert_no_error (error);
        g_assert (ret);

        g_assert_cmpstr (gcm_edid_get_monitor_name (edid), ==, NULL);
        g_assert_cmpstr (gcm_edid_get_vendor_name (edid), ==, "IBM Brasil");
        g_assert_cmpstr (gcm_edid_get_serial_number (edid), ==, NULL);
        g_assert_cmpstr (gcm_edid_get_eisa_id (edid), ==, "LTN154P2-L05");
        g_assert_cmpstr (gcm_edid_get_checksum (edid), ==, "e1865128c7cd5e5ed49ecfc8102f6f9c");
        g_assert_cmpstr (gcm_edid_get_pnp_id (edid), ==, "IBM");
        g_assert_cmpint (gcm_edid_get_height (edid), ==, 21);
        g_assert_cmpint (gcm_edid_get_width (edid), ==, 33);
        g_assert_cmpfloat (gcm_edid_get_gamma (edid), >=, 2.2f - 0.01);
        g_assert_cmpfloat (gcm_edid_get_gamma (edid), <, 2.2f + 0.01);
        g_free (data);

        g_object_unref (edid);
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

        tz = g_time_zone_new ("+01:30");
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
        g_assert (gsd_night_light_frac_day_is_between (12, 6, 20));
        g_assert (!gsd_night_light_frac_day_is_between (5, 6, 20));
        g_assert (gsd_night_light_frac_day_is_between (12, 0, 24));
        g_assert (gsd_night_light_frac_day_is_between (12, -1, 25));

        /* test rollover to next day */
        g_assert (gsd_night_light_frac_day_is_between (23, 20, 6));
        g_assert (!gsd_night_light_frac_day_is_between (12, 20, 6));

        /* test rollover to the previous day */
        g_assert (gsd_night_light_frac_day_is_between (5, 16, 8));
}

int
main (int argc, char **argv)
{
        char *schema_dir;

        g_setenv ("GSETTINGS_BACKEND", "memory", TRUE);

        gtk_init (&argc, &argv);
        g_test_init (&argc, &argv, NULL);

        schema_dir = g_test_build_filename (G_TEST_BUILT, "", NULL);
        g_setenv("GSETTINGS_SCHEMA_DIR", schema_dir, TRUE);
        g_free (schema_dir);

        g_test_add_func ("/color/edid", gcm_test_edid_func);
        g_test_add_func ("/color/sunset-sunrise", gcm_test_sunset_sunrise);
        g_test_add_func ("/color/sunset-sunrise/fractional-timezone", gcm_test_sunset_sunrise_fractional_timezone);
        g_test_add_func ("/color/fractional-day", gcm_test_frac_day);
        g_test_add_func ("/color/night-light", gcm_test_night_light);

        return g_test_run ();
}

