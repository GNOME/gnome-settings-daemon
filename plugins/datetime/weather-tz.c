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

#include "weather-tz.h"
#include "tz.h"

#define GWEATHER_I_KNOW_THIS_IS_UNSTABLE
#include <libgweather/gweather.h>

struct _WeatherTzDB
{
        GList *tz_locations;
};

static GList *
location_get_cities (GWeatherLocation *parent_location)
{
        GList *cities = NULL;
        GWeatherLocation **children;
        gint i;

        children = gweather_location_get_children (parent_location);
        for (i = 0; children[i]; i++) {
                if (gweather_location_get_level (children[i]) == GWEATHER_LOCATION_CITY) {
                        cities = g_list_prepend (cities,
                                                 children[i]);
                } else {
                        cities = g_list_concat (cities,
                                                location_get_cities (children[i]));
                }
        }

        return cities;
}

static gboolean
weather_location_has_timezone (GWeatherLocation *loc)
{
        return gweather_location_get_timezone (loc) != NULL;
}

/**
 * load_timezones:
 * @cities: a list of #GWeatherLocation
 *
 * Returns: a list of #TzLocation
 */
static GList *
load_timezones (GList *cities)
{
        GList *l;
        GList *tz_locations = NULL;

        for (l = cities; l; l = l->next) {
                TzLocation *loc;
                const gchar *country;
                const gchar *timezone_id;
                gdouble latitude;
                gdouble longitude;

                if (!gweather_location_has_coords (l->data) ||
                    !weather_location_has_timezone (l->data)) {
                        g_debug ("Incomplete GWeather location entry: (%s) %s",
                                 gweather_location_get_country (l->data),
                                 gweather_location_get_city_name (l->data));
                        continue;
                }

                country = gweather_location_get_country (l->data);
                timezone_id = gweather_timezone_get_tzid (gweather_location_get_timezone (l->data));
                gweather_location_get_coords (l->data,
                                              &latitude,
                                              &longitude);

                loc = g_new0 (TzLocation, 1);
                loc->country = g_strdup (country);
                loc->latitude = latitude;
                loc->longitude = longitude;
                loc->zone = g_strdup (timezone_id);
                loc->comment = NULL;

                tz_locations = g_list_prepend (tz_locations, loc);
        }

        return tz_locations;
}

GList *
weather_tz_db_get_locations (WeatherTzDB *tzdb)
{
        return g_list_copy (tzdb->tz_locations);
}

WeatherTzDB *
weather_tz_db_new (void)
{
        GList *cities;
        GWeatherLocation *world;
        WeatherTzDB *tzdb;

        world = gweather_location_get_world ();
        cities = location_get_cities (world);

        tzdb = g_new0 (WeatherTzDB, 1);
        tzdb->tz_locations = load_timezones (cities);

        g_list_free (cities);

        return tzdb;
}

void
weather_tz_db_free (WeatherTzDB *tzdb)
{
        g_list_free_full (tzdb->tz_locations, (GDestroyNotify) tz_location_free);

        g_free (tzdb);
}
