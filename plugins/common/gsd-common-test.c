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

#include <glib.h>
#include <glib-object.h>
#include <stdlib.h>

#include "gsd-datetime-helper.h"

static void
gsd_test_sunset_sunrise (void)
{
        gdouble sunrise;
        gdouble sunrise_actual = 7.6;
        gdouble sunset;
        gdouble sunset_actual = 16.8;
        g_autoptr(GDateTime) dt = g_date_time_new_utc (2007, 2, 1, 0, 0, 0);

        /* get for London, today */
        gsd_datetime_get_sunrise_sunset (dt, 51.5, -0.1278, &sunrise, &sunset);
        g_assert_cmpfloat (sunrise, <, sunrise_actual + 0.1);
        g_assert_cmpfloat (sunrise, >, sunrise_actual - 0.1);
        g_assert_cmpfloat (sunset, <, sunset_actual + 0.1);
        g_assert_cmpfloat (sunset, >, sunset_actual - 0.1);
}

static void
gsd_test_sunset_sunrise_fractional_timezone (void)
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
        gsd_datetime_get_sunrise_sunset (dt, 51.5, -0.1278, &sunrise, &sunset);
        g_assert_cmpfloat (sunrise, <, sunrise_actual + 0.1);
        g_assert_cmpfloat (sunrise, >, sunrise_actual - 0.1);
        g_assert_cmpfloat (sunset, <, sunset_actual + 0.1);
        g_assert_cmpfloat (sunset, >, sunset_actual - 0.1);
}

static void
gsd_test_frac_day (void)
{
        g_autoptr(GDateTime) dt = g_date_time_new_utc (2007, 2, 1, 12, 59, 59);
        gdouble fd;
        gdouble fd_actual = 12.99;

        /* test for 12:59:59 */
        fd = gsd_datetime_frac_day_from_dt (dt);
        g_assert_cmpfloat (fd, >, fd_actual - 0.01);
        g_assert_cmpfloat (fd, <, fd_actual + 0.01);

        /* test same day */
        g_assert_true (gsd_datetime_frac_day_is_between (12, 6, 20));
        g_assert_false (gsd_datetime_frac_day_is_between (5, 6, 20));
        g_assert_true (gsd_datetime_frac_day_is_between (12, 0, 24));
        g_assert_true (gsd_datetime_frac_day_is_between (12, -1, 25));

        /* test rollover to next day */
        g_assert_true (gsd_datetime_frac_day_is_between (23, 20, 6));
        g_assert_false (gsd_datetime_frac_day_is_between (12, 20, 6));

        /* test rollover to the previous day */
        g_assert_true (gsd_datetime_frac_day_is_between (5, 16, 8));

        /* test equality */
        g_assert_true (gsd_datetime_frac_day_is_between (12, 0.5, 24.5));
        g_assert_true (gsd_datetime_frac_day_is_between (0.5, 0.5, 0.5));
}

int
main (int argc, char **argv)
{
        g_test_init (&argc, &argv, NULL);

        g_test_add_func ("/common/sunset-sunrise", gsd_test_sunset_sunrise);
        g_test_add_func ("/common/sunset-sunrise/fractional-timezone", gsd_test_sunset_sunrise_fractional_timezone);
        g_test_add_func ("/common/fractional-day", gsd_test_frac_day);

        return g_test_run ();
}

