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
#include <geoclue.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include "gnome-datetime-source.h"

#include "gsd-color-scheme.h"
#include "gsd-datetime-helper.h"

struct _GsdColorScheme {
    GObject              parent;

    GSettings           *scheme_settings;
    GSettings           *interface_settings;
    GSettings           *location_settings;

    GClueClient         *geoclue_client;
    GClueSimple         *geoclue_simple;
    GCancellable        *geoclue_cancellable;

    GSource             *source;
    guint                validate_id;
    gdouble              cached_sunrise;
    gdouble              cached_sunset;

    GDateTime           *datetime_override;
};

enum {
    PROP_0,
    PROP_SUNRISE,
    PROP_SUNSET,
    PROP_LAST
};

enum {
    SCHEME_SWITCHED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

#define GSD_COLOR_SCHEME_SCHEDULE_TIMEOUT   5              /* seconds */
#define GSD_FRAC_DAY_MAX_DELTA              (1.f/60.f)     /* 1 minute */

#define DESKTOP_ID "gnome-background-panel"

static void timeout_destroy (GsdColorScheme *self);
static void timeout_create  (GsdColorScheme *self);
static void recheck         (GsdColorScheme *self);

G_DEFINE_TYPE (GsdColorScheme, gsd_color_scheme, G_TYPE_OBJECT);

static GDateTime *
gsd_color_scheme_get_date_time_now (GsdColorScheme *self)
{
    if (self->datetime_override != NULL)
        return g_date_time_ref (self->datetime_override);
    return g_date_time_new_now_local ();
}

void
gsd_color_scheme_set_date_time_now (GsdColorScheme *self,
                                    GDateTime      *datetime)
{
    if (self->datetime_override != NULL)
        g_date_time_unref (self->datetime_override);
    self->datetime_override = g_date_time_ref (datetime);

    recheck (self);
}

static gboolean
update_cached_schedule (GsdColorScheme *self)
{
    gboolean ret = FALSE;
    gdouble latitude;
    gdouble longitude;
    gdouble sunrise;
    gdouble sunset;
    g_autoptr(GVariant) tmp = NULL;
    g_autoptr(GDateTime) dt_now = gsd_color_scheme_get_date_time_now (self);

    /* calculate the sunrise/sunset for the location */
    tmp = g_settings_get_value (self->scheme_settings, "last-coordinates");
    g_variant_get (tmp, "(dd)", &latitude, &longitude);
    if (latitude > 90.f || latitude < -90.f)
        return FALSE;
    if (longitude > 180.f || longitude < -180.f)
        return FALSE;
    if (!gsd_datetime_get_sunrise_sunset (dt_now, latitude, longitude,
                                          &sunrise, &sunset)) {
        g_warning ("failed to get sunset/sunrise for %.3f,%.3f",
                   longitude, longitude);
        return FALSE;
    }

    /* anything changed */
    if (ABS (self->cached_sunrise - sunrise) > GSD_FRAC_DAY_MAX_DELTA) {
        self->cached_sunrise = sunrise;
        g_object_notify (G_OBJECT (self), "sunrise");
        ret = TRUE;
    }
    if (ABS (self->cached_sunset - sunset) > GSD_FRAC_DAY_MAX_DELTA) {
        self->cached_sunset = sunset;
        g_object_notify (G_OBJECT (self), "sunset");
        ret = TRUE;
    }
    return ret;
}

static void
set_scheme (GsdColorScheme *self,
            gboolean        is_dark)
{
    GDesktopColorScheme system_scheme;
    GDesktopColorScheme new_scheme;

    system_scheme = g_settings_get_enum (self->interface_settings, "color-scheme");

    if (is_dark)
        new_scheme = G_DESKTOP_COLOR_SCHEME_PREFER_DARK;
    else
        new_scheme = G_DESKTOP_COLOR_SCHEME_DEFAULT;
    
    /* Don't needlessly transition */
    if (system_scheme == new_scheme)
        return;

    g_debug ("Changing scheme from %i to %i (1 = dark, 2 = light)", system_scheme, new_scheme);

    g_signal_emit (G_OBJECT (self), signals[SCHEME_SWITCHED], 0);
    
    g_settings_set_enum (self->interface_settings, "color-scheme", new_scheme);
}

static void
recheck (GsdColorScheme *self)
{
    gdouble frac_day;
    gdouble schedule_from = -1.f;
    gdouble schedule_to = -1.f;
    g_autoptr(GDateTime) dt_now = gsd_color_scheme_get_date_time_now (self);

    /* enabled */
    if (!g_settings_get_boolean (self->scheme_settings, "enabled")) {
        g_debug ("Color Scheme disabled, resetting");
        return;
    }

    /* Calculate position of sun */
    if (g_settings_get_boolean (self->scheme_settings, "schedule-automatic")) {
        update_cached_schedule (self);
        if (self->cached_sunrise > 0.f && self->cached_sunset > 0.f) {
            schedule_to = self->cached_sunrise;
            schedule_from = self->cached_sunset;
        }
    }

    /* Manual */
    if (schedule_to <= 0.f || schedule_from <= 0.f) {
        schedule_from = g_settings_get_double (self->scheme_settings,
                                               "schedule-from");
        schedule_to = g_settings_get_double (self->scheme_settings,
                                             "schedule-to");
    }

    /* get the current hour of a day as a fraction */
    frac_day = gsd_datetime_frac_day_from_dt (dt_now);
    g_debug ("fractional day = %.3f, dark = %.3f->%.3f",
             frac_day, schedule_from, schedule_to);
    
    if (!gsd_datetime_frac_day_is_between (frac_day,
                                           schedule_from,
                                           schedule_to)) {
        set_scheme (self, FALSE);
        return;
    }

    set_scheme (self, TRUE);
}

static void
scheme_settings_changed_cb (GsdColorScheme *self,
                           GSettings       *settings,
                           gchar           *key)
{
    g_debug ("Color Scheme settings changed");
    recheck (self);
}

static gboolean
time_changed_recheck_cb (gpointer user_data)
{
    GsdColorScheme *self = GSD_COLOR_SCHEME (user_data);

    /* recheck parameters, then reschedule a new timeout */
    recheck (self);
    timeout_destroy (self);
    timeout_create (self);

    /* return value ignored for a one-time watch */
    return G_SOURCE_REMOVE;
}

static void
timeout_create (GsdColorScheme *self)
{
    g_autoptr(GDateTime) dt_now = NULL;
    g_autoptr(GDateTime) dt_expiry = NULL;

    if (self->source != NULL)
        return;
    
    dt_now = g_date_time_new_now_local ();
    dt_expiry = g_date_time_add_seconds (dt_now, GSD_COLOR_SCHEME_SCHEDULE_TIMEOUT);
    self->source = _gnome_datetime_source_new (dt_now,
                                                  dt_expiry,
                                                  TRUE);
    g_source_set_callback (self->source,
                           time_changed_recheck_cb,
                           self, NULL);
    g_source_attach (self->source, NULL);
}

static void
timeout_destroy (GsdColorScheme *self)
{
    if (self->source == NULL)
        return;

    g_source_destroy (self->source);
    g_source_unref (self->source);
    self->source = NULL;
}

static gboolean
recheck_schedule_cb (gpointer user_data)
{
    GsdColorScheme *self = GSD_COLOR_SCHEME (user_data);
    recheck (self);
    self->validate_id = 0;
    return G_SOURCE_REMOVE;
}

/* called when something changed */
static void
recheck_schedule (GsdColorScheme *self)
{
    if (self->validate_id != 0)
        g_source_remove (self->validate_id);
    self->validate_id =
        g_timeout_add_seconds (GSD_COLOR_SCHEME_SCHEDULE_TIMEOUT,
                               recheck_schedule_cb,
                               self);
}

static void
on_location_notify (GClueSimple *simple,
                    GParamSpec  *pspec,
                    gpointer     user_data)
{
    GsdColorScheme *self = GSD_COLOR_SCHEME (user_data);
    GClueLocation *location;
    gdouble latitude, longitude;

    location = gclue_simple_get_location (simple);
    latitude = gclue_location_get_latitude (location);
    longitude = gclue_location_get_longitude (location);

    g_settings_set_value (self->scheme_settings,
                          "last-coordinates",
                          g_variant_new ("(dd)", latitude, longitude));

    g_debug ("got geoclue latitude %f, longitude %f", latitude, longitude);

    /* recheck the levels if the location changed significantly */
    if (update_cached_schedule (self))
        recheck_schedule (self);
}

static void
on_geoclue_ready (GObject      *source,
                  GAsyncResult *res,
                  gpointer      user_data)
{
    GsdColorScheme *self = GSD_COLOR_SCHEME (user_data);
    GClueSimple *geoclue_simple;
    g_autoptr(GError) error = NULL;

    geoclue_simple = gclue_simple_new_finish (res, &error);
    if (geoclue_simple == NULL) {
        if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            g_warning ("Failed to connect to GeoClue2 service: %s", error->message);
        return;
    } else {
        g_debug ("Established connection to Geoclue");
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
start_geoclue (GsdColorScheme *self)
{
    self->geoclue_cancellable = g_cancellable_new ();

    gclue_simple_new (DESKTOP_ID,
                      GCLUE_ACCURACY_LEVEL_CITY,
                      self->geoclue_cancellable,
                      on_geoclue_ready,
                      self);
}

static void
stop_geoclue (GsdColorScheme *self)
{
    g_cancellable_cancel (self->geoclue_cancellable);
    g_clear_object (&self->geoclue_cancellable);

    if (self->geoclue_client != NULL) {
        gclue_client_call_stop (self->geoclue_client, NULL, NULL, NULL);
        self->geoclue_client = NULL;
    }
    g_clear_object (&self->geoclue_simple);
}

static void
check_location_settings (GsdColorScheme *self)
{
    if (g_settings_get_boolean (self->location_settings, "enabled"))
        start_geoclue (self);
    else
        stop_geoclue (self);
}

static void
gsd_color_scheme_finalize (GObject *object)
{
    GsdColorScheme *self;

    g_return_if_fail (object != NULL);
    g_return_if_fail (GSD_IS_COLOR_SCHEME (object));

    self = GSD_COLOR_SCHEME (object);

    if (self->validate_id > 0) {
        g_source_remove (self->validate_id);
        self->validate_id = 0;
    }

    g_clear_object (&self->scheme_settings);
    g_clear_object (&self->interface_settings);
    g_clear_object (&self->location_settings);

    G_OBJECT_CLASS (gsd_color_scheme_parent_class)->finalize (object);
}

static void
gsd_color_scheme_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    GsdColorScheme *self = GSD_COLOR_SCHEME (object);

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
gsd_color_scheme_get_property (GObject      *object,
                               guint         prop_id,
                               GValue       *value,
                               GParamSpec   *pspec)
{
    GsdColorScheme *self = GSD_COLOR_SCHEME (object);

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
gsd_color_scheme_class_init (GsdColorSchemeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = gsd_color_scheme_finalize;

    object_class->set_property = gsd_color_scheme_set_property;
    object_class->get_property = gsd_color_scheme_get_property;

    signals[SCHEME_SWITCHED] =
        g_signal_new ("scheme-switched",
                      G_TYPE_FROM_CLASS (object_class),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

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
}

static void
gsd_color_scheme_init (GsdColorScheme *self)
{
    self->scheme_settings = g_settings_new ("org.gnome.settings-daemon.plugins.color-scheme");
    self->interface_settings = g_settings_new ("org.gnome.desktop.interface");
    self->location_settings = g_settings_new ("org.gnome.system.location");

    self->cached_sunrise = -1.f;
    self->cached_sunset = -1.f;
}

GsdColorScheme *
gsd_color_scheme_new (void)
{
    return g_object_new (GSD_TYPE_COLOR_SCHEME, NULL);
}

gboolean
gsd_color_scheme_start (GsdColorScheme *self, GError **error)
{
    recheck (self);
    timeout_create (self);

    /* Notify on any settings changes */
    g_signal_connect_swapped (self->scheme_settings, "changed",
                              G_CALLBACK (scheme_settings_changed_cb), self);
    g_signal_connect_swapped (self->location_settings, "changed::enabled",
                              G_CALLBACK (check_location_settings), self);
    check_location_settings (self);

    return TRUE;
}

gdouble
gsd_color_scheme_get_sunrise (GsdColorScheme *self)
{
    return self->cached_sunrise;
}

gdouble
gsd_color_scheme_get_sunset (GsdColorScheme *self)
{
    return self->cached_sunset;
}