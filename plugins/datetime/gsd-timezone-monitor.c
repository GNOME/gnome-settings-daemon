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

#include "geoclue.h"
#include "timedated.h"
#include "tz.h"
#include "weather-tz.h"

#include <geocode-glib/geocode-glib.h>
#include <polkit/polkit.h>

#define DESKTOP_ID "gnome-datetime-panel"
#define SET_TIMEZONE_PERMISSION "org.freedesktop.timedate1.set-timezone"

/* Defines from geoclue private header src/public-api/gclue-enums.h */
#define GCLUE_ACCURACY_LEVEL_CITY 4

enum {
        TIMEZONE_CHANGED,
        LAST_SIGNAL
};

static int signals[LAST_SIGNAL] = { 0 };

typedef struct
{
        GCancellable *cancellable;
        GPermission *permission;
        GeoclueClient *geoclue_client;
        GeoclueManager *geoclue_manager;
        Timedate1 *dtm;

        TzDB *tzdb;
        WeatherTzDB *weather_tzdb;
        gchar *current_timezone;
} GsdTimezoneMonitorPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsdTimezoneMonitor, gsd_timezone_monitor, G_TYPE_OBJECT)

static void
set_timezone_cb (GObject      *source,
                 GAsyncResult *res,
                 gpointer      user_data)
{
        GsdTimezoneMonitor *self = user_data;
        GsdTimezoneMonitorPrivate *priv = gsd_timezone_monitor_get_instance_private (self);
        GError *error = NULL;

        if (!timedate1_call_set_timezone_finish (priv->dtm,
                                                 res,
                                                 &error)) {
                g_warning ("Could not set system timezone: %s", error->message);
                g_error_free (error);
                return;
        }

        g_signal_emit (G_OBJECT (self),
                       signals[TIMEZONE_CHANGED],
                       0, priv->current_timezone);
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
compare_locations (TzLocation *a,
                   TzLocation *b)
{
        if (a->dist > b->dist)
                return 1;

        if (a->dist < b->dist)
                return -1;

        return 0;
}

static GList *
sort_by_closest_to (GList           *locations,
                    GeocodeLocation *location)
{
        GList *l;

        for (l = locations; l; l = l->next) {
                GeocodeLocation *loc;
                TzLocation *tz_location = l->data;

                loc = geocode_location_new (tz_location->latitude,
                                            tz_location->longitude,
                                            GEOCODE_LOCATION_ACCURACY_UNKNOWN);
                tz_location->dist = geocode_location_get_distance_from (loc, location);
                g_object_unref (loc);
        }

        return g_list_sort (locations, (GCompareFunc) compare_locations);
}

static GList *
ptr_array_to_list (GPtrArray *array)
{
        GList *l = NULL;
        gint i;

        for (i = 0; i < array->len; i++)
                l = g_list_prepend (l, g_ptr_array_index (array, i));

        return l;
}

static GList *
find_by_country (GList       *locations,
                 const gchar *country_code)
{
        GList *l, *found = NULL;
        gchar *c1;
        gchar *c2;

        c1 = g_ascii_strdown (country_code, -1);

        for (l = locations; l; l = l->next) {
                TzLocation *loc = l->data;

                c2 = g_ascii_strdown (loc->country, -1);
                if (g_strcmp0 (c1, c2) == 0)
                        found = g_list_prepend (found, loc);
                g_free (c2);
        }
        g_free (c1);

        return found;
}

static const gchar *
find_timezone (GsdTimezoneMonitor *self,
               GeocodeLocation    *location,
               const gchar        *country_code)
{
        GList *filtered;
        GList *locations;
        GsdTimezoneMonitorPrivate *priv = gsd_timezone_monitor_get_instance_private (self);
        TzLocation *closest_tz_location;

        /* First load locations from Olson DB */
        locations = ptr_array_to_list (tz_get_locations (priv->tzdb));
        g_return_val_if_fail (locations != NULL, NULL);

        /* ... and then add libgweather's locations as well */
        locations = g_list_concat (locations,
                                   weather_tz_db_get_locations (priv->weather_tzdb));

        /* Filter tz locations by country */
        filtered = find_by_country (locations, country_code);
        if (filtered != NULL) {
                g_list_free (locations);
                locations = filtered;
        } else {
                g_debug ("No match for country code '%s' in tzdb", country_code);
        }

        /* Find the closest tz location */
        locations = sort_by_closest_to (locations, location);
        closest_tz_location = (TzLocation *) locations->data;

        g_list_free (locations);

        return closest_tz_location->zone;
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

        if (g_strcmp0 (priv->current_timezone, new_timezone) != 0)
                queue_set_timezone (self, new_timezone);
}

static void
on_reverse_geocoding_ready (GObject      *source_object,
                            GAsyncResult *res,
                            gpointer      user_data)
{
        GeocodePlace *place;
        GError *error = NULL;
        GsdTimezoneMonitor *self = user_data;

        place = geocode_reverse_resolve_finish (GEOCODE_REVERSE (source_object),
                                                res,
                                                &error);
        if (error != NULL) {
                g_debug ("Reverse geocoding failed: %s", error->message);
                g_error_free (error);
                return;
        }
        g_debug ("Geocode lookup resolved country to '%s'",
                 geocode_place_get_country (place));

        process_location (self, place);
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
                                       priv->cancellable,
                                       on_reverse_geocoding_ready,
                                       self);

        g_object_unref (location);
        g_object_unref (reverse);
}

static void
on_location_proxy_ready (GObject      *source_object,
                         GAsyncResult *res,
                         gpointer      user_data)
{
        GeoclueLocation *location;
        gdouble latitude, longitude;
        GError *error = NULL;
        GsdTimezoneMonitor *self = user_data;

        location = geoclue_location_proxy_new_for_bus_finish (res, &error);
        if (error != NULL) {
                g_critical ("Failed to connect to GeoClue2 service: %s", error->message);
                g_error_free (error);
                return;
        }

        latitude = geoclue_location_get_latitude (location);
        longitude = geoclue_location_get_longitude (location);

        start_reverse_geocoding (self, latitude, longitude);

        g_object_unref (location);
}

static void
on_location_updated (GDBusProxy *client,
                     gchar      *location_path_old,
                     gchar      *location_path_new,
                     gpointer    user_data)
{
        GsdTimezoneMonitor *self = user_data;
        GsdTimezoneMonitorPrivate *priv = gsd_timezone_monitor_get_instance_private (self);

        geoclue_location_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                            G_DBUS_PROXY_FLAGS_NONE,
                                            "org.freedesktop.GeoClue2",
                                            location_path_new,
                                            priv->cancellable,
                                            on_location_proxy_ready,
                                            self);
}

static void
on_start_ready (GObject      *source_object,
                GAsyncResult *res,
                gpointer      user_data)
{
        GError *error = NULL;

        if (!geoclue_client_call_start_finish (GEOCLUE_CLIENT (source_object),
                                               res,
                                               &error)) {
                g_critical ("Failed to start GeoClue2 client: %s", error->message);
                g_error_free (error);
                return;
        }
}

static void
on_client_proxy_ready (GObject      *source_object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
        GError *error = NULL;
        GsdTimezoneMonitor *self = user_data;
        GsdTimezoneMonitorPrivate *priv = gsd_timezone_monitor_get_instance_private (self);

        priv->geoclue_client = geoclue_client_proxy_new_for_bus_finish (res, &error);
        if (error != NULL) {
                g_critical ("Failed to connect to GeoClue2 service: %s", error->message);
                g_error_free (error);
                return;
        }

        geoclue_client_set_desktop_id (priv->geoclue_client, DESKTOP_ID);
        geoclue_client_set_distance_threshold (priv->geoclue_client,
                                               GEOCODE_LOCATION_ACCURACY_CITY);
        geoclue_client_set_requested_accuracy_level (priv->geoclue_client,
                                                     GCLUE_ACCURACY_LEVEL_CITY);

        g_signal_connect (priv->geoclue_client, "location-updated",
                          G_CALLBACK (on_location_updated), self);

        geoclue_client_call_start (priv->geoclue_client,
                                   priv->cancellable,
                                   on_start_ready,
                                   self);
}

static void
on_get_client_ready (GObject      *source_object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
        gchar *client_path;
        GError *error = NULL;
        GsdTimezoneMonitor *self = user_data;
        GsdTimezoneMonitorPrivate *priv = gsd_timezone_monitor_get_instance_private (self);

        if (!geoclue_manager_call_get_client_finish (GEOCLUE_MANAGER (source_object),
                                                     &client_path,
                                                     res,
                                                     &error)) {
                g_critical ("Failed to connect to GeoClue2 service: %s", error->message);
                g_error_free (error);
                return;
        }

        geoclue_client_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                          G_DBUS_PROXY_FLAGS_NONE,
                                          "org.freedesktop.GeoClue2",
                                          client_path,
                                          priv->cancellable,
                                          on_client_proxy_ready,
                                          self);
}

static void
on_manager_proxy_ready (GObject      *source_object,
                        GAsyncResult *res,
                        gpointer      user_data)
{

        GError *error = NULL;
        GsdTimezoneMonitor *self = user_data;
        GsdTimezoneMonitorPrivate *priv = gsd_timezone_monitor_get_instance_private (self);

        priv->geoclue_manager = geoclue_manager_proxy_new_for_bus_finish (res, &error);
        if (error != NULL) {
                g_critical ("Failed to connect to GeoClue2 service: %s", error->message);
                g_error_free (error);
                return;
        }

        geoclue_manager_call_get_client (priv->geoclue_manager,
                                         priv->cancellable,
                                         on_get_client_ready,
                                         self);
}

static void
register_geoclue (GsdTimezoneMonitor *self)
{
        GsdTimezoneMonitorPrivate *priv = gsd_timezone_monitor_get_instance_private (self);

        geoclue_manager_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                           G_DBUS_PROXY_FLAGS_NONE,
                                           "org.freedesktop.GeoClue2",
                                           "/org/freedesktop/GeoClue2/Manager",
                                           priv->cancellable,
                                           on_manager_proxy_ready,
                                           self);
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

        if (priv->cancellable) {
                g_cancellable_cancel (priv->cancellable);
                g_clear_object (&priv->cancellable);
        }

        if (priv->geoclue_client) {
                geoclue_client_call_stop (priv->geoclue_client, NULL, NULL, NULL);
                g_clear_object (&priv->geoclue_client);
        }

        g_clear_object (&priv->dtm);
        g_clear_object (&priv->geoclue_manager);
        g_clear_object (&priv->permission);
        g_clear_pointer (&priv->current_timezone, g_free);
        g_clear_pointer (&priv->tzdb, tz_db_free);
        g_clear_pointer (&priv->weather_tzdb, weather_tz_db_free);

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

        register_geoclue (self);
}
