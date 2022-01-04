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

#include <libgweather/gweather.h>

static GList *
location_get_cities (GWeatherLocation *parent_location)
{
        GList *cities = NULL;
        GWeatherLocation *child = NULL;

        while ((child = gweather_location_next_child (parent_location, child))) {
                if (gweather_location_get_level (child) == GWEATHER_LOCATION_CITY) {
                        cities = g_list_prepend (cities, g_object_ref (child));
                } else {
                        cities = g_list_concat (cities,
                                                location_get_cities (child));
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
                GTimeZone *tz;
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
                tz = gweather_location_get_timezone (l->data);
                timezone_id = g_time_zone_get_identifier (tz);
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
weather_tz_db_get_locations (const gchar *country_code)
{
        g_autoptr(GWeatherLocation) world = NULL;
        g_autoptr(GWeatherLocation) country = NULL;
        g_autolist(GWeatherLocation) cities = NULL;
        GList *tz_locations;

        world = gweather_location_get_world ();

        country = gweather_location_find_by_country_code (world, country_code);

        if (!country)
                return NULL;

        cities = location_get_cities (country);
        tz_locations = load_timezones (cities);

        return tz_locations;
}
