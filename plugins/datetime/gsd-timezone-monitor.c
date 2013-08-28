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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include "gsd-timezone-monitor.h"

#include "geoclue.h"
#include "timedated.h"
#include "tz.h"

#include <math.h>
#include <polkit/polkit.h>

#define SET_TIMEZONE_PERMISSION "org.freedesktop.timedate1.set-timezone"

typedef struct
{
        GCancellable *cancellable;
        GPermission *permission;
        GeoclueClient *geoclue_client;
        GeoclueManager *geoclue_manager;
        Timedate1 *dtm;

        TzDB *tzdb;
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
        }
}

static void
queue_set_timezone (GsdTimezoneMonitor *self,
                    const char *timezone)
{
        GsdTimezoneMonitorPrivate *priv = gsd_timezone_monitor_get_instance_private (self);

        timedate1_call_set_timezone (priv->dtm,
                                     timezone,
                                     TRUE,
                                     priv->cancellable,
                                     set_timezone_cb,
                                     self);
}


#define EARTH_RADIUS_KM 6372.795

/* Copy of geocode_location_get_distance_from() from geocode-glib */
static double
distance_between (gdouble loca_latitude, gdouble loca_longitude,
                  gdouble locb_latitude, gdouble locb_longitude)
{
        gdouble dlat, dlon, lat1, lat2;
        gdouble a, c;

        /* Algorithm from:
         * http://www.movable-type.co.uk/scripts/latlong.html */

        dlat = (locb_latitude - loca_latitude) * M_PI / 180.0;
        dlon = (locb_longitude - loca_longitude) * M_PI / 180.0;
        lat1 = loca_latitude * M_PI / 180.0;
        lat2 = locb_latitude * M_PI / 180.0;

        a = sin (dlat / 2) * sin (dlat / 2) +
            sin (dlon / 2) * sin (dlon / 2) * cos (lat1) * cos (lat2);
        c = 2 * atan2 (sqrt (a), sqrt (1-a));
        return EARTH_RADIUS_KM * c;
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

static void
process_location (GsdTimezoneMonitor *self,
                  gdouble             latitude,
                  gdouble             longitude)
{
        GsdTimezoneMonitorPrivate *priv = gsd_timezone_monitor_get_instance_private (self);
        GPtrArray *array;
        GList *distances = NULL;
        TzLocation *closest_tz_location;
        gint i;

        array = tz_get_locations (priv->tzdb);

        for (i = 0; i < array->len; i++) {
                TzLocation *loc = array->pdata[i];

                loc->dist = distance_between (latitude, longitude,
                                              loc->latitude, loc->longitude);
                distances = g_list_prepend (distances, loc);

        }
        distances = g_list_sort (distances, (GCompareFunc) compare_locations);

        closest_tz_location = (TzLocation*) distances->data;
        if (g_strcmp0 (priv->current_timezone, closest_tz_location->zone) != 0) {
                g_debug ("Changing timezone to %s", closest_tz_location->zone);
                queue_set_timezone (self, closest_tz_location->zone);
        }

        g_list_free (distances);
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

        process_location (self, latitude, longitude);

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

        g_clear_object (&priv->dtm);
        g_clear_object (&priv->geoclue_client);
        g_clear_object (&priv->geoclue_manager);
        g_clear_object (&priv->permission);
        g_clear_pointer (&priv->current_timezone, g_free);
        g_clear_pointer (&priv->tzdb, tz_db_free);

        G_OBJECT_CLASS (gsd_timezone_monitor_parent_class)->finalize (obj);
}

static void
gsd_timezone_monitor_class_init (GsdTimezoneMonitorClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_timezone_monitor_finalize;
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

        register_geoclue (self);
}
