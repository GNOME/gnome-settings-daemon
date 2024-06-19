/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2024-2025 Jamie Murphy <jmurphy@gnome.org>
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
 */

#include "config.h"

#include <gdesktop-enums.h>
#include "gsd-color-scheme.h"

static void
on_notify (GsdColorScheme *color_scheme,
           GParamSpec     *pspec,
           gpointer        user_data)
{
    guint *cnt = (guint *) user_data;
    (*cnt)++;
}

static void
gsd_test_color_scheme (void)
{
    gboolean ret;
    guint sunrise_cnt = 0;
    guint sunset_cnt = 0;
    g_autoptr(GsdColorScheme) color_scheme = NULL;
    g_autoptr(GSettings) settings = NULL;
    g_autoptr(GSettings) scheme_settings = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GDateTime) datetime_override = NULL;

    color_scheme = gsd_color_scheme_new ();
    g_assert (GSD_IS_COLOR_SCHEME (color_scheme));
    g_signal_connect (color_scheme, "notify::sunset",
                      G_CALLBACK (on_notify), &sunset_cnt);
    g_signal_connect (color_scheme, "notify::sunrise",
                      G_CALLBACK (on_notify), &sunrise_cnt);
    
    // TODO: disable geoclue
    /* hardcode a specific date and time */
    datetime_override = g_date_time_new_utc (2017, 2, 8, 20, 0, 0);
    gsd_color_scheme_set_date_time_now (color_scheme, datetime_override);

    /* switch off */
    settings = g_settings_new ("org.gnome.settings-daemon.plugins.color-scheme");
    scheme_settings = g_settings_new ("org.gnome.desktop.interface");
    g_settings_set_boolean (settings, "enabled", FALSE);
    g_settings_set_enum (scheme_settings, "color-scheme", G_DESKTOP_COLOR_SCHEME_PREFER_DARK);

    /* check default values */
    g_assert_cmpint ((gint) gsd_color_scheme_get_sunrise (color_scheme), ==, -1);
    g_assert_cmpint ((gint) gsd_color_scheme_get_sunset (color_scheme), ==, -1);

    /* start module, disabled */
    ret = gsd_color_scheme_start (color_scheme, &error);
    g_assert_no_error (error);
    g_assert (ret);
    g_assert_cmpint (sunset_cnt, ==, 0);
    g_assert_cmpint (sunrise_cnt, ==, 0);

    /* enable automatic mode */
    g_settings_set_value (settings, "last-coordinates",
                          g_variant_new ("(dd)", 51.5, -0.1278));
    g_settings_set_boolean (settings, "schedule-automatic", TRUE);
    g_settings_set_boolean (settings, "enabled", TRUE);
    g_assert_cmpint (sunset_cnt, ==, 1);
    g_assert_cmpint (sunrise_cnt, ==, 1);
    g_assert_cmpint ((gint) gsd_color_scheme_get_sunrise (color_scheme), ==, 7);
    g_assert_cmpint ((gint) gsd_color_scheme_get_sunset (color_scheme), ==, 17);
    g_assert_cmpint ((gint) g_settings_get_enum (scheme_settings, "color-scheme"), ==, 1); // Dark

    /* enabled manual mode */
    g_settings_set_double (settings, "schedule-from", 4.0);
    g_settings_set_double (settings, "schedule-to", 16.f);
    g_settings_set_boolean (settings, "schedule-automatic", FALSE);
    g_assert_cmpint (sunset_cnt, ==, 1);
    g_assert_cmpint (sunrise_cnt, ==, 1);
    g_assert_cmpint ((gint) gsd_color_scheme_get_sunrise (color_scheme), ==, 7);
    g_assert_cmpint ((gint) gsd_color_scheme_get_sunset (color_scheme), ==, 17);
    g_assert_cmpint ((gint) g_settings_get_enum (scheme_settings, "color-scheme"), ==, 2); // Light

    /* disable, with no changes */
    g_settings_set_boolean (settings, "enabled", FALSE);
    g_assert_cmpint (sunset_cnt, ==, 1);
    g_assert_cmpint (sunrise_cnt, ==, 1);
    g_assert_cmpint ((gint) g_settings_get_enum (scheme_settings, "color-scheme"), ==, 2); // Light

    /* enable and adjust schedule */
    g_settings_set_boolean (settings, "enabled", TRUE);
    g_clear_pointer (&datetime_override, g_date_time_unref);
    datetime_override = g_date_time_new_utc (2017, 2, 8, 4, 00, 00);
    gsd_color_scheme_set_date_time_now (color_scheme, datetime_override);
    g_assert_cmpint ((gint) g_settings_get_enum (scheme_settings, "color-scheme"), ==, 1); // Dark

    /* adjust schedule again */
    g_clear_pointer (&datetime_override, g_date_time_unref);
    datetime_override = g_date_time_new_utc (2017, 2, 8, 3, 00, 00);
    gsd_color_scheme_set_date_time_now (color_scheme, datetime_override);
    g_assert_cmpint ((gint) g_settings_get_enum (scheme_settings, "color-scheme"), ==, 2); // Light

    /* Check that we are always dark if from/to are equal */
    g_settings_set_double (settings, "schedule-from", 6.0);
    g_settings_set_double (settings, "schedule-to", 6.0);

    datetime_override = g_date_time_new_utc (2017, 2, 8, 5, 50, 0);
    gsd_color_scheme_set_date_time_now (color_scheme, datetime_override);
    g_assert_cmpint ((gint) g_settings_get_enum (scheme_settings, "color-scheme"), ==, 1); // Dark

    datetime_override = g_date_time_new_utc (2017, 2, 8, 6, 0, 0);
    gsd_color_scheme_set_date_time_now (color_scheme, datetime_override);
    g_assert_cmpint ((gint) g_settings_get_enum (scheme_settings, "color-scheme"), ==, 1); // Dark

    datetime_override = g_date_time_new_utc (2017, 2, 8, 6, 10, 0);
    gsd_color_scheme_set_date_time_now (color_scheme, datetime_override);
    g_assert_cmpint ((gint) g_settings_get_enum (scheme_settings, "color-scheme"), ==, 1); // Dark
}

int
main (int argc, char **argv)
{
    g_setenv ("GSETTINGS_BACKEND", "memory", TRUE);

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/color-scheme", gsd_test_color_scheme);

    return g_test_run ();
}