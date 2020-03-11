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

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <dlfcn.h>

#include "weather-tz.h"
#include "tz.h"

#define GWEATHER_I_KNOW_THIS_IS_UNSTABLE
#include <libgweather/gweather.h>

struct _WeatherTzDB
{
        GArray *tz_locations;
};

static void
load_timezones (GArray           *tz_locations,
                GWeatherLocation *location)
{
        if (location == NULL)
                return;

        if (gweather_location_get_level (location) == GWEATHER_LOCATION_CITY &&
            gweather_location_has_coords (location) &&
            gweather_location_get_timezone (location) != NULL)
        {
                const gchar *country;
                const gchar *timezone_id;
                gdouble latitude;
                gdouble longitude;
                TzLocation loc;

                country = gweather_location_get_country (location);
                timezone_id = gweather_timezone_get_tzid (gweather_location_get_timezone (location));
                gweather_location_get_coords (location, &latitude, &longitude);

                loc.latitude = latitude;
                loc.longitude = longitude;
                loc.dist = 0;
                loc.zone = tz_intern (timezone_id);
                loc.country = tz_intern (country);

                g_array_append_val (tz_locations, loc);
        }
        else
        {
                GWeatherLocation **children = gweather_location_get_children (location);
                for (guint i = 0; children[i] != NULL; i++)
                        load_timezones (tz_locations, children[i]);
        }
}

void
weather_tz_db_populate_locations (WeatherTzDB *tzdb,
                                  GArray      *ar,
                                  const char  *country_code)
{
	if (country_code == NULL) {
		if (tzdb->tz_locations->len > 0)
			g_array_append_vals (ar,
			                     &g_array_index (tzdb->tz_locations, TzLocation, 0),
			                     tzdb->tz_locations->len);
		return;
	}

        for (guint i = 0; i < tzdb->tz_locations->len; i++) {
                const TzLocation *loc = &g_array_index (tzdb->tz_locations, TzLocation, i);

                if (tz_location_is_country_code (loc, country_code))
                        g_array_append_vals (ar, loc, 1);
        }
}

static void
release_world (void)
{
	void (*release_world_func) (void) = NULL;

	/* libgweather holds on to a great deal of memory in the default
	 * world instance. We do not need this after parsing, so we can release
	 * it all by finding the exported _gweather_location_reset_world() symbol
	 * and calling it.
	 *
	 * This should really be fixed in libgweather, but to prove it's an issue,
	 * we can test things here first.
	 */

	release_world_func = dlsym (RTLD_NEXT, "_gweather_location_reset_world");
	if (release_world_func != NULL) {
		g_debug ("Releasing Locations.xml data");
		release_world_func ();
	}
	else {
		g_debug ("Cannot locate symbol _gweather_location_reset_world()");
	}
}

WeatherTzDB *
weather_tz_db_new (void)
{
        WeatherTzDB *tzdb;

        tzdb = g_new0 (WeatherTzDB, 1);
        tzdb->tz_locations = g_array_new (FALSE, FALSE, sizeof (TzLocation));
        load_timezones (tzdb->tz_locations, gweather_location_get_world ());

	release_world ();

        return tzdb;
}

void
weather_tz_db_free (WeatherTzDB *tzdb)
{
        g_array_unref (tzdb->tz_locations);
        g_free (tzdb);
}
