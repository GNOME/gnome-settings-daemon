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

#include "gsd-timezone-monitor.h"

#include "timedated.h"
#include "tz.h"
#include "weather-tz.h"

#include <geoclue.h>
#include <geocode-glib/geocode-glib.h>
#include <polkit/polkit.h>

#define DESKTOP_ID "gnome-datetime-panel"
#define SET_TIMEZONE_PERMISSION "org.freedesktop.timedate1.set-timezone"

enum {
        TIMEZONE_CHANGED,
        LAST_SIGNAL
};

static int signals[LAST_SIGNAL] = { 0 };

typedef struct
{
        GCancellable *cancellable;
        GPermission *permission;
        Timedate1 *dtm;

        GClueClient *geoclue_client;
        GClueSimple *geoclue_simple;
        GCancellable *geoclue_cancellable;

        TzDB *tzdb;
        WeatherTzDB *weather_tzdb;
        gchar *current_timezone;

        GSettings *location_settings;
} GsdTimezoneMonitorPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsdTimezoneMonitor, gsd_timezone_monitor, G_TYPE_OBJECT)

static void
set_timezone_cb (GObject      *source,
                 GAsyncResult *res,
                 gpointer      user_data)
{
        GsdTimezoneMonitorPrivate *priv;
        GError *error = NULL;

        if (!timedate1_call_set_timezone_finish (TIMEDATE1 (source),
                                                 res,
                                                 &error)) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Could not set system timezone: %s", error->message);
                g_error_free (error);
                return;
        }

        priv = gsd_timezone_monitor_get_instance_private (user_data);
        g_signal_emit (G_OBJECT (user_data),
                       signals[TIMEZONE_CHANGED],
                       0, priv->current_timezone);

        g_debug ("Successfully changed timezone to '%s'",
                 priv->current_timezone);
}

static void
queue_set_timezone (GsdTimezoneMonitor *self,
                    const gchar        *new_timezone)
{
        GsdTimezoneMonitorPrivate *priv = gsd_timezone_monitor_get_instance_private (self);

        g_debug ("Changing timezone to '%s'", new_timezone);

        timedate1_call_set_timezone (priv->dtm,
                                     new_timezone,
                                     TRUE,
                                     priv->cancellable,
                                     set_timezone_cb,
                                     self);

        g_free (priv->current_timezone);
        priv->current_timezone = g_strdup (new_timezone);
}

static gint
compare_locations (const TzLocation *a,
                   const TzLocation *b)
{
        if (a->dist > b->dist)
                return 1;

        if (a->dist < b->dist)
                return -1;

        return 0;
}

static void
sort_by_closest_to (GArray          *locations,
                    GeocodeLocation *location)
{
        for (guint i = 0; i < locations->len; i++) {
                TzLocation *tzloc = &g_array_index (locations, TzLocation, i);
                g_autoptr(GeocodeLocation) loc = NULL;

                loc = geocode_location_new (tzloc->latitude,
                                            tzloc->longitude,
                                            GEOCODE_LOCATION_ACCURACY_UNKNOWN);
                tzloc->dist = geocode_location_get_distance_from (loc, location);
        }

        g_array_sort (locations, (GCompareFunc) compare_locations);
}

static const gchar *
find_timezone (GsdTimezoneMonitor *self,
               GeocodeLocation    *location,
               const gchar        *country_code)
{
        GsdTimezoneMonitorPrivate *priv = gsd_timezone_monitor_get_instance_private (self);
        g_autoptr(GArray) locations = g_array_new (FALSE, FALSE, sizeof (TzLocation));
        TzLocation closest_tz_location;

        /* First load locations from Olson DB */
        tz_populate_locations (priv->tzdb, locations, country_code);

        /* ... and then add libgweather's locations as well */
        weather_tz_db_populate_locations (priv->weather_tzdb, locations, country_code);

        if (locations->len == 0) {
                g_debug ("No match for country code '%s' in tzdb", country_code);

		/* Populate with all of the items instead of by country */
		tz_populate_locations (priv->tzdb, locations, NULL);
		weather_tz_db_populate_locations (priv->weather_tzdb, locations, NULL);
        }

        /* Find the closest tz location */
        sort_by_closest_to (locations, location);
        closest_tz_location = g_array_index (locations, TzLocation, 0);

        return closest_tz_location.zone;
}

static void
process_location (GsdTimezoneMonitor *self,
                  GeocodePlace       *place)
{
        GeocodeLocation *location;
        GsdTimezoneMonitorPrivate *priv = gsd_timezone_monitor_get_instance_private (self);
        const gchar *country_code;
        const gchar *new_timezone;

        country_code = geocode_place_get_country_code (place);
        location = geocode_place_get_location (place);

        new_timezone = find_timezone (self, location, country_code);

        if (g_strcmp0 (priv->current_timezone, new_timezone) != 0) {
                g_debug ("Found updated timezone '%s' for country '%s'",
                         new_timezone, country_code);
                queue_set_timezone (self, new_timezone);
        } else {
                g_debug ("Timezone didn't change from '%s' for country '%s'",
                         new_timezone, country_code);
        }
}

static void
on_reverse_geocoding_ready (GObject      *source_object,
                            GAsyncResult *res,
                            gpointer      user_data)
{
        GeocodePlace *place;
        GError *error = NULL;

        place = geocode_reverse_resolve_finish (GEOCODE_REVERSE (source_object),
                                                res,
                                                &error);
        if (error != NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_debug ("Reverse geocoding failed: %s", error->message);
                g_error_free (error);
                return;
        }
        g_debug ("Geocode lookup resolved country to '%s'",
                 geocode_place_get_country (place));

        process_location (user_data, place);
        g_object_unref (place);
}

static void
start_reverse_geocoding (GsdTimezoneMonitor *self,
                         gdouble             latitude,
                         gdouble             longitude)
{
        GeocodeLocation *location;
        GeocodeReverse *reverse;
        GsdTimezoneMonitorPrivate *priv = gsd_timezone_monitor_get_instance_private (self);

        location = geocode_location_new (latitude,
                                         longitude,
                                         GEOCODE_LOCATION_ACCURACY_CITY);

        reverse = geocode_reverse_new_for_location (location);
        geocode_reverse_resolve_async (reverse,
                                       priv->geoclue_cancellable,
                                       on_reverse_geocoding_ready,
                                       self);

        g_object_unref (location);
        g_object_unref (reverse);
}

static void
on_location_notify (GClueSimple *simple,
                    GParamSpec  *pspec,
                    gpointer     user_data)
{
        GsdTimezoneMonitor *self = user_data;
        GClueLocation *location;
        gdouble latitude, longitude;

        location = gclue_simple_get_location (simple);

        latitude = gclue_location_get_latitude (location);
        longitude = gclue_location_get_longitude (location);

        g_debug ("Got location %lf,%lf", latitude, longitude);

        start_reverse_geocoding (self, latitude, longitude);
}

static void
on_geoclue_simple_ready (GObject      *source_object,
                         GAsyncResult *res,
                         gpointer      user_data)
{
        GError *error = NULL;
        GsdTimezoneMonitorPrivate *priv;
        GClueSimple *geoclue_simple;

        geoclue_simple = gclue_simple_new_finish (res, &error);
        if (geoclue_simple == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Failed to connect to GeoClue2 service: %s", error->message);
                g_error_free (error);
                return;
        }

        g_debug ("Geoclue now available");

        priv = gsd_timezone_monitor_get_instance_private (user_data);
        priv->geoclue_simple = geoclue_simple;
        priv->geoclue_client = gclue_simple_get_client (priv->geoclue_simple);
        gclue_client_set_distance_threshold (priv->geoclue_client,
                                             GEOCODE_LOCATION_ACCURACY_CITY);

        g_signal_connect (priv->geoclue_simple, "notify::location",
                          G_CALLBACK (on_location_notify), user_data);

        on_location_notify (priv->geoclue_simple, NULL, user_data);
}

static void
start_geoclue (GsdTimezoneMonitor *self)
{
        GsdTimezoneMonitorPrivate *priv = gsd_timezone_monitor_get_instance_private (self);

        g_debug ("Timezone monitor enabled, starting geoclue");

        priv->geoclue_cancellable = g_cancellable_new ();
        gclue_simple_new (DESKTOP_ID,
                          GCLUE_ACCURACY_LEVEL_CITY,
                          priv->geoclue_cancellable,
                          on_geoclue_simple_ready,
                          self);

}

static void
stop_geoclue (GsdTimezoneMonitor *self)
{
        GsdTimezoneMonitorPrivate *priv = gsd_timezone_monitor_get_instance_private (self);

        g_debug ("Timezone monitor disabled, stopping geoclue");

        g_cancellable_cancel (priv->geoclue_cancellable);
        g_clear_object (&priv->geoclue_cancellable);

        if (priv->geoclue_client) {
                gclue_client_call_stop (priv->geoclue_client, NULL, NULL, NULL);
                priv->geoclue_client = NULL;
        }

        g_clear_object (&priv->geoclue_simple);
}

GsdTimezoneMonitor *
gsd_timezone_monitor_new (void)
{
        return g_object_new (GSD_TYPE_TIMEZONE_MONITOR, NULL);
}

static void
gsd_timezone_monitor_finalize (GObject *obj)
{
        GsdTimezoneMonitor *monitor = GSD_TIMEZONE_MONITOR (obj);
        GsdTimezoneMonitorPrivate *priv = gsd_timezone_monitor_get_instance_private (monitor);

        g_debug ("Stopping timezone monitor");

        stop_geoclue (monitor);

        if (priv->cancellable) {
                g_cancellable_cancel (priv->cancellable);
                g_clear_object (&priv->cancellable);
        }

        g_clear_object (&priv->dtm);
        g_clear_object (&priv->permission);
        g_clear_pointer (&priv->current_timezone, g_free);
        g_clear_pointer (&priv->tzdb, tz_db_free);
        g_clear_pointer (&priv->weather_tzdb, weather_tz_db_free);

        g_clear_object (&priv->location_settings);

        G_OBJECT_CLASS (gsd_timezone_monitor_parent_class)->finalize (obj);
}

static void
gsd_timezone_monitor_class_init (GsdTimezoneMonitorClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_timezone_monitor_finalize;

        signals[TIMEZONE_CHANGED] =
                g_signal_new ("timezone-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GsdTimezoneMonitorClass, timezone_changed),
                              NULL, NULL,
                              NULL,
                              G_TYPE_NONE, 1, G_TYPE_POINTER);
}

static void
check_location_settings (GsdTimezoneMonitor *self)
{
        GsdTimezoneMonitorPrivate *priv = gsd_timezone_monitor_get_instance_private (self);
        if (g_settings_get_boolean (priv->location_settings, "enabled"))
                start_geoclue (self);
        else
                stop_geoclue (self);
}

static void
gsd_timezone_monitor_init (GsdTimezoneMonitor *self)
{
        GError *error = NULL;
        GsdTimezoneMonitorPrivate *priv = gsd_timezone_monitor_get_instance_private (self);

        g_debug ("Starting timezone monitor");

        priv->permission = polkit_permission_new_sync (SET_TIMEZONE_PERMISSION,
                                                       NULL, NULL,
                                                       &error);
        if (priv->permission == NULL) {
                g_warning ("Could not get '%s' permission: %s",
                           SET_TIMEZONE_PERMISSION,
                           error->message);
                g_error_free (error);
                return;
        }

        if (!g_permission_get_allowed (priv->permission)) {
                g_debug ("No permission to set timezone");
                return;
        }

        priv->cancellable = g_cancellable_new ();
        priv->dtm = timedate1_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                      G_DBUS_PROXY_FLAGS_NONE,
                                                      "org.freedesktop.timedate1",
                                                      "/org/freedesktop/timedate1",
                                                      priv->cancellable,
                                                      &error);
        if (priv->dtm == NULL) {
                g_warning ("Could not get proxy for DateTimeMechanism: %s", error->message);
                g_error_free (error);
                return;
        }

        priv->current_timezone = timedate1_dup_timezone (priv->dtm);
        priv->tzdb = tz_load_db ();
        priv->weather_tzdb = weather_tz_db_new ();

        priv->location_settings = g_settings_new ("org.gnome.system.location");
        g_signal_connect_swapped (priv->location_settings, "changed::enabled",
                                  G_CALLBACK (check_location_settings), self);
        check_location_settings (self);
}
