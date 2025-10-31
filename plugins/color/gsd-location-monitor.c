/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2025 Jamie Murphy <jmurphy@gnome.org>
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

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include "gnome-datetime-source.h"

#include "gsd-night-light-common.h"
#include "gsd-location-monitor.h"

#include <geoclue.h>

struct _GsdLocationMonitor {
    GObject            parent;

    GSettings         *settings;
    GSettings         *location_settings;

    gdouble            cached_sunrise;
    gdouble            cached_sunset;
    GDateTime         *datetime_override;

    GClueClient       *geoclue_client;
    GClueSimple       *geoclue_simple;
    gboolean           geoclue_enabled;
    GCancellable      *cancellable;
};

enum {
    PROP_0,
    PROP_SUNRISE,
    PROP_SUNSET,
    PROP_LAST
};

enum {
    CHANGED,
    LAST_SIGNAL
};

static int signals[LAST_SIGNAL] = { 0 };

#define GSD_FRAC_DAY_MAX_DELTA          (1.f/60.f)      /* 1 minute */

#define DESKTOP_ID "gnome-color-panel"

static void update_cached_sunrise_sunset (GsdLocationMonitor *self);

G_DEFINE_TYPE (GsdLocationMonitor, gsd_location_monitor, G_TYPE_OBJECT);

GDateTime *
gsd_location_monitor_get_date_time_now (GsdLocationMonitor *self)
{
    if (self->datetime_override != NULL)
        return g_date_time_ref (self->datetime_override);
    return g_date_time_new_now_local ();
}

void
gsd_location_monitor_set_date_time_now (GsdLocationMonitor *self,
                                        GDateTime *datetime)
{
    if (self->datetime_override != NULL)
        g_date_time_unref (self->datetime_override);
    self->datetime_override = g_date_time_ref (datetime);

    update_cached_sunrise_sunset (self);
}

static void
update_cached_sunrise_sunset (GsdLocationMonitor *self)
{
    gdouble latitude;
    gdouble longitude;
    gdouble sunrise;
    gdouble sunset;
    g_autoptr(GVariant) tmp = NULL;
    g_autoptr(GDateTime) dt_now = gsd_location_monitor_get_date_time_now (self);

    /* calculate the sunrise/sunset for the location */
    tmp = g_settings_get_value (self->settings, "night-light-last-coordinates");
    g_variant_get (tmp, "(dd)", &latitude, &longitude);
    if (latitude > 90.f || latitude < -90.f)
        return;
    if (longitude > 180.f || longitude < -180.f)
        return;
    if (!gsd_night_light_get_sunrise_sunset (dt_now, latitude, longitude,
                                          &sunrise, &sunset)) {
        g_warning ("failed to get sunset/sunrise for %.3f,%.3f",
                   longitude, longitude);
        return;
    }

    /* anything changed */
    if (ABS (self->cached_sunrise - sunrise) > GSD_FRAC_DAY_MAX_DELTA) {
        self->cached_sunrise = sunrise;
        g_object_notify (G_OBJECT (self), "sunrise");
    }
    if (ABS (self->cached_sunset - sunset) > GSD_FRAC_DAY_MAX_DELTA) {
        self->cached_sunset = sunset;
        g_object_notify (G_OBJECT (self), "sunset");
    }
}

static void
settings_changed_cb (GSettings *settings, gchar *key, gpointer user_data)
{
    GsdLocationMonitor *self = GSD_LOCATION_MONITOR (user_data);
    g_debug ("settings changed");
    update_cached_sunrise_sunset (self);
    g_signal_emit (G_OBJECT (self),
                   signals[CHANGED],
                   0);
}

static void
on_location_notify (GClueSimple *simple,
                    GParamSpec  *pspec,
                    gpointer     user_data)
{
    GsdLocationMonitor *self = GSD_LOCATION_MONITOR (user_data);
    GClueLocation *location;
    gdouble latitude, longitude;

    location = gclue_simple_get_location (simple);
    latitude = gclue_location_get_latitude (location);
    longitude = gclue_location_get_longitude (location);

    g_settings_set_value (self->settings,
                          "night-light-last-coordinates",
                          g_variant_new ("(dd)", latitude, longitude));

    g_debug ("got geoclue latitude %f, longitude %f", latitude, longitude);

    /* recheck the levels if the location changed significantly */
    update_cached_sunrise_sunset (self);
}

static void
on_geoclue_simple_ready (GObject      *source_object,
                         GAsyncResult *res,
                         gpointer      user_data)
{
    GsdLocationMonitor *self = GSD_LOCATION_MONITOR (user_data);
    GClueSimple *geoclue_simple;
    g_autoptr(GError) error = NULL;

    geoclue_simple = gclue_simple_new_finish (res, &error);
    if (geoclue_simple == NULL) {
        if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            g_warning ("Failed to connect to GeoClue2 service: %s", error->message);
        return;
    }

    self->geoclue_simple = geoclue_simple;
    self->geoclue_client = gclue_simple_get_client (self->geoclue_simple);
    g_object_set (G_OBJECT (self->geoclue_client),
                  "time-threshold", 60*60, NULL); /* 1 hour */

    g_signal_connect (self->geoclue_simple, "notify::location",
                      G_CALLBACK (on_location_notify), user_data);

    on_location_notify (self->geoclue_simple, NULL, user_data);
}

static void
start_geoclue (GsdLocationMonitor *self)
{
    self->cancellable = g_cancellable_new ();
    gclue_simple_new (DESKTOP_ID,
                      GCLUE_ACCURACY_LEVEL_CITY,
                      self->cancellable,
                      on_geoclue_simple_ready,
                      self);

}

static void
stop_geoclue (GsdLocationMonitor *self)
{
    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);

    if (self->geoclue_client != NULL) {
            gclue_client_call_stop (self->geoclue_client, NULL, NULL, NULL);
            self->geoclue_client = NULL;
    }
    g_clear_object (&self->geoclue_simple);
}

static void
check_location_settings (GsdLocationMonitor *self)
{
    if (g_settings_get_boolean (self->location_settings, "enabled") && self->geoclue_enabled)
        start_geoclue (self);
    else
        stop_geoclue (self);
}

gdouble
gsd_location_monitor_get_sunrise (GsdLocationMonitor *self)
{
        return self->cached_sunrise;
}

gdouble
gsd_location_monitor_get_sunset (GsdLocationMonitor *self)
{
        return self->cached_sunset;
}

void
gsd_location_monitor_set_geoclue_enabled (GsdLocationMonitor *self,
                                          gboolean            enabled)
{
    self->geoclue_enabled = enabled;
}

gboolean
gsd_location_monitor_start (GsdLocationMonitor  *self,
                            GError             **error)
{
    g_signal_connect (self->settings, "changed",
                      G_CALLBACK (settings_changed_cb), self);
    g_signal_connect_swapped (self->location_settings, "changed::enabled",
                              G_CALLBACK (check_location_settings), self);
    check_location_settings (self);

    return TRUE;
}

static void
gsd_location_monitor_finalize (GObject *object)
{
    GsdLocationMonitor *self = GSD_LOCATION_MONITOR (object);

    stop_geoclue (self);

    g_clear_object (&self->settings);
    g_clear_object (&self->location_settings);
    g_clear_pointer (&self->datetime_override, g_date_time_unref);

    G_OBJECT_CLASS (gsd_location_monitor_parent_class)->finalize (object);
}

static void
gsd_location_monitor_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    GsdLocationMonitor *self = GSD_LOCATION_MONITOR (object);

    switch (prop_id) {
    case PROP_SUNRISE:
        self->cached_sunrise = g_value_get_double (value);
        break;
    case PROP_SUNSET:
        self->cached_sunset = g_value_get_double (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gsd_location_monitor_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    GsdLocationMonitor *self = GSD_LOCATION_MONITOR (object);

    switch (prop_id) {
    case PROP_SUNRISE:
        g_value_set_double (value, self->cached_sunrise);
        break;
    case PROP_SUNSET:
        g_value_set_double (value, self->cached_sunset);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gsd_location_monitor_class_init (GsdLocationMonitorClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = gsd_location_monitor_finalize;

    object_class->set_property = gsd_location_monitor_set_property;
    object_class->get_property = gsd_location_monitor_get_property;

    g_object_class_install_property (object_class,
                                     PROP_SUNRISE,
                                     g_param_spec_double ("sunrise",
                                                          "Sunrise",
                                                          "Sunrise in fractional hours",
                                                          0,
                                                          24.f,
                                                          12,
                                                          G_PARAM_READWRITE));

    g_object_class_install_property (object_class,
                                     PROP_SUNSET,
                                     g_param_spec_double ("sunset",
                                                          "Sunset",
                                                          "Sunset in fractional hours",
                                                          0,
                                                          24.f,
                                                          12,
                                                          G_PARAM_READWRITE));

    signals[CHANGED] =
        g_signal_new ("changed",
                      G_TYPE_FROM_CLASS (object_class),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);                                                       
}

static void
gsd_location_monitor_init (GsdLocationMonitor *self)
{
    self->geoclue_enabled = TRUE;
    self->cached_sunrise = -1.f;
    self->cached_sunset = -1.f;
    self->settings = g_settings_new ("org.gnome.settings-daemon.plugins.color");
    self->location_settings = g_settings_new ("org.gnome.system.location");
}

GsdLocationMonitor *
gsd_location_monitor_get (void)
{
    static GsdLocationMonitor *instance;

    if (g_once_init_enter (&instance)) {
        GsdLocationMonitor *monitor = g_object_new (GSD_TYPE_LOCATION_MONITOR, NULL);
        g_object_add_weak_pointer (G_OBJECT (monitor), (gpointer *)&instance);
        g_once_init_leave (&instance, monitor);
    }

    return g_object_ref (instance);
}