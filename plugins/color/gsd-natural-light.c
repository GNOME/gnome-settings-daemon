/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
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

#include <geoclue.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include "gnome-datetime-source.h"

#include "gsd-color-state.h"

#include "gsd-natural-light.h"
#include "gsd-natural-light-common.h"

struct _GsdNaturalLight {
        GObject            parent;
        GSettings         *settings;
        gboolean           disabled_until_tmw;
        gboolean           geoclue_enabled;
        GSource           *natural_light_source;
        guint              natural_light_validate_id;
        gint               disabled_day_of_month;
        GClueClient       *geoclue_client;
        GClueSimple       *geoclue_simple;
        gdouble            cached_sunrise;
        gdouble            cached_sunset;
        gdouble            cached_temperature;
        GCancellable      *cancellable;
        GDateTime         *datetime_override;
};

enum {
        PROP_0,
        PROP_SUNRISE,
        PROP_SUNSET,
        PROP_TEMPERATURE,
        PROP_DISABLED_UNTIL_TMW,
        PROP_LAST
};

#define GSD_NATURAL_LIGHT_SCHEDULE_TIMEOUT      5       /* seconds */
#define GSD_NATURAL_LIGHT_POLL_TIMEOUT          60      /* seconds */
#define GSD_NATURAL_LIGHT_POLL_SMEAR            1       /* hours */

#define GSD_FRAC_DAY_MAX_DELTA                  (1.f/60.f)     /* 1 minute */
#define GSD_TEMPERATURE_MAX_DELTA               (10.f)          /* Kelvin */

#define DESKTOP_ID "gnome-color-panel"

static void poll_timeout_destroy (GsdNaturalLight *self);
static void poll_timeout_create (GsdNaturalLight *self);

G_DEFINE_TYPE (GsdNaturalLight, gsd_natural_light, G_TYPE_OBJECT);

static GDateTime *
gsd_natural_light_get_date_time_now (GsdNaturalLight *self)
{
        if (self->datetime_override != NULL)
                return g_date_time_ref (self->datetime_override);
        return g_date_time_new_now_local ();
}

void
gsd_natural_light_set_date_time_now (GsdNaturalLight *self, GDateTime *datetime)
{
        if (self->datetime_override != NULL)
                g_date_time_unref (self->datetime_override);
        self->datetime_override = g_date_time_ref (datetime);
}

static gdouble
linear_interpolate (gdouble val1, gdouble val2, gdouble factor)
{
        g_return_val_if_fail (factor > 0.f, -1.f);
        g_return_val_if_fail (factor < 1.f, -1.f);
        return ((val1 - val2) * factor) + val2;
}

static gboolean
update_cached_sunrise_sunset (GsdNaturalLight *self)
{
        gboolean ret = FALSE;
        gdouble latitude;
        gdouble longitude;
        gdouble sunrise;
        gdouble sunset;
        g_autoptr(GVariant) tmp = NULL;
        g_autoptr(GDateTime) dt_now = gsd_natural_light_get_date_time_now (self);

        /* calculate the sunrise/sunset for the location */
        tmp = g_settings_get_value (self->settings, "natural-light-last-coordinates");
        g_variant_get (tmp, "(dd)", &latitude, &longitude);
        if (latitude > 180.f || latitude < -180.f)
                return FALSE;
        if (longitude > 180.f || longitude < -180.f)
                return FALSE;
        if (!gsd_natural_light_get_sunrise_sunset (dt_now, latitude, longitude,
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
gsd_natural_light_set_temperature (GsdNaturalLight *self, gdouble temperature)
{
        if (ABS (self->cached_temperature - temperature) > GSD_TEMPERATURE_MAX_DELTA) {
                self->cached_temperature = temperature;
                g_object_notify (G_OBJECT (self), "temperature");
        }
}

static void
natural_light_recheck (GsdNaturalLight *self)
{
        gdouble frac_day;
        gdouble schedule_from = -1.f;
        gdouble schedule_to = -1.f;
        gdouble smear = GSD_NATURAL_LIGHT_POLL_SMEAR; /* hours */
        guint temperature;
        guint temp_smeared;
        g_autoptr(GDateTime) dt_now = gsd_natural_light_get_date_time_now (self);

        /* enabled */
        if (!g_settings_get_boolean (self->settings, "natural-light-enabled")) {
                g_debug ("natural light disabled, resetting");
                gsd_natural_light_set_temperature (self,
                                                   GSD_COLOR_TEMPERATURE_DEFAULT);
                return;
        }

        /* disabled until tomorrow */
        if (self->disabled_until_tmw) {
                gint tmp_day_of_month = g_date_time_get_day_of_month (dt_now);
                if (tmp_day_of_month == self->disabled_day_of_month) {
                        g_debug ("natural light still day-disabled, resetting");
                        gsd_natural_light_set_temperature (self,
                                                         GSD_COLOR_TEMPERATURE_DEFAULT);
                        return;
                }

                /* no longer valid */
                self->disabled_day_of_month = 0;
                self->disabled_until_tmw = FALSE;
                g_object_notify (G_OBJECT (self), "disabled-until-tmw");
        }

        /* calculate the position of the sun */
        if (g_settings_get_boolean (self->settings, "natural-light-schedule-automatic")) {
                update_cached_sunrise_sunset (self);
                if (self->cached_sunrise > 0.f && self->cached_sunset > 0.f) {
                        schedule_to = self->cached_sunrise;
                        schedule_from = self->cached_sunset;
                }
        }

        /* fall back to manual settings */
        if (schedule_to <= 0.f || schedule_from <= 0.f) {
                schedule_from = g_settings_get_double (self->settings,
                                                       "natural-light-schedule-from");
                schedule_to = g_settings_get_double (self->settings,
                                                     "natural-light-schedule-to");
        }

        /* get the current hour of a day as a fraction */
        frac_day = gsd_natural_light_frac_day_from_dt (dt_now);
        g_debug ("fractional day = %.3f, limits = %.3f->%.3f",
                 frac_day, schedule_from, schedule_to);
        if (!gsd_natural_light_frac_day_is_between (frac_day,
                                                    schedule_from - smear,
                                                    schedule_to)) {
                g_debug ("not time for natural-light");
                gsd_natural_light_set_temperature (self,
                                                 GSD_COLOR_TEMPERATURE_DEFAULT);
                return;
        }

        /* smear the temperature for a short duration before the set limits
         *
         *   |----------------------| = from->to
         * |-|                        = smear down
         *                        |-| = smear up
         *
         * \                        /
         *  \                      /
         *   \--------------------/
         */
        temperature = g_settings_get_uint (self->settings, "natural-light-temperature");
        if (gsd_natural_light_frac_day_is_between (frac_day,
                                                   schedule_from - smear,
                                                   schedule_from)) {
                gdouble factor = 1.f - ((frac_day - (schedule_from - smear)) / smear);
                temp_smeared = linear_interpolate (GSD_COLOR_TEMPERATURE_DEFAULT,
                                                   temperature, factor);
        } else if (gsd_natural_light_frac_day_is_between (frac_day,
                                                          schedule_to - smear,
                                                          schedule_to)) {
                gdouble factor = ((schedule_to - smear) - frac_day) / smear;
                temp_smeared = linear_interpolate (GSD_COLOR_TEMPERATURE_DEFAULT,
                                                   temperature, factor);
        } else {
                temp_smeared = temperature;
        }
        g_debug ("natural light mode on, using temperature of %uK (aiming for %uK)",
                 temp_smeared, temperature);
        gsd_natural_light_set_temperature (self, temp_smeared);
}

static gboolean
natural_light_recheck_schedule_cb (gpointer user_data)
{
        GsdNaturalLight *self = GSD_NATURAL_LIGHT (user_data);
        natural_light_recheck (self);
        self->natural_light_validate_id = 0;
        return G_SOURCE_REMOVE;
}

/* called when something changed */
static void
natural_light_recheck_schedule (GsdNaturalLight *self)
{
        if (self->natural_light_validate_id != 0)
                g_source_remove (self->natural_light_validate_id);
        self->natural_light_validate_id =
                g_timeout_add_seconds (GSD_NATURAL_LIGHT_SCHEDULE_TIMEOUT,
                                       natural_light_recheck_schedule_cb,
                                       self);
}

/* called when the time may have changed */
static gboolean
natural_light_recheck_cb (gpointer user_data)
{
        GsdNaturalLight *self = GSD_NATURAL_LIGHT (user_data);

        /* recheck parameters, then reschedule a new timeout */
        natural_light_recheck (self);
        poll_timeout_destroy (self);
        poll_timeout_create (self);

        /* return value ignored for a one-time watch */
        return G_SOURCE_REMOVE;
}

static void
poll_timeout_create (GsdNaturalLight *self)
{
        g_autoptr(GDateTime) dt_now = NULL;
        g_autoptr(GDateTime) dt_expiry = NULL;

        if (self->natural_light_source != NULL)
                return;

        dt_now = gsd_natural_light_get_date_time_now (self);
        dt_expiry = g_date_time_add_seconds (dt_now, GSD_NATURAL_LIGHT_POLL_TIMEOUT);
        self->natural_light_source = _gnome_datetime_source_new (dt_now,
                                                                 dt_expiry,
                                                                 FALSE);
        g_source_set_callback (self->natural_light_source,
                               natural_light_recheck_cb,
                               self, NULL);
        g_source_attach (self->natural_light_source, NULL);
}

static void
poll_timeout_destroy (GsdNaturalLight *self)
{

        if (self->natural_light_source == NULL)
                return;

        g_source_destroy (self->natural_light_source);
        self->natural_light_source = NULL;
}

static void
settings_changed_cb (GSettings *settings, gchar *key, gpointer user_data)
{
        GsdNaturalLight *self = GSD_NATURAL_LIGHT (user_data);
        g_debug ("settings changed");
        natural_light_recheck (self);
}

static void
on_location_notify (GClueSimple *simple,
                    GParamSpec  *pspec,
                    gpointer     user_data)
{
        GsdNaturalLight *self = GSD_NATURAL_LIGHT (user_data);
        GClueLocation *location;
        gdouble latitude, longitude;

        location = gclue_simple_get_location (simple);
        latitude = gclue_location_get_latitude (location);
        longitude = gclue_location_get_longitude (location);

        g_settings_set_value (self->settings,
                              "natural-light-last-coordinates",
                              g_variant_new ("(dd)", latitude, longitude));

        g_debug ("got geoclue latitude %f, longitude %f", latitude, longitude);

        /* recheck the levels if the location changed significantly */
        if (update_cached_sunrise_sunset (self))
                natural_light_recheck_schedule (self);
}

static void
on_geoclue_simple_ready (GObject      *source_object,
                         GAsyncResult *res,
                         gpointer      user_data)
{
        GsdNaturalLight *self = GSD_NATURAL_LIGHT (user_data);
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

        g_signal_connect (self->geoclue_simple, "notify::location",
                          G_CALLBACK (on_location_notify), user_data);

        on_location_notify (self->geoclue_simple, NULL, user_data);
}

static void
register_geoclue (GsdNaturalLight *self)
{
        gclue_simple_new (DESKTOP_ID,
                          GCLUE_ACCURACY_LEVEL_CITY,
                          self->cancellable,
                          on_geoclue_simple_ready,
                          self);

}

void
gsd_natural_light_set_disabled_until_tmw (GsdNaturalLight *self, gboolean value)
{
        g_autoptr(GDateTime) dt = gsd_natural_light_get_date_time_now (self);

        if (self->disabled_until_tmw == value)
                return;

        self->disabled_until_tmw = value;
        self->disabled_day_of_month = g_date_time_get_day_of_month (dt);
        natural_light_recheck (self);
        g_object_notify (G_OBJECT (self), "disabled-until-tmw");
}

gboolean
gsd_natural_light_get_disabled_until_tmw (GsdNaturalLight *self)
{
        return self->disabled_until_tmw;
}

gdouble
gsd_natural_light_get_sunrise (GsdNaturalLight *self)
{
        return self->cached_sunrise;
}

gdouble
gsd_natural_light_get_sunset (GsdNaturalLight *self)
{
        return self->cached_sunset;
}

gdouble
gsd_natural_light_get_temperature (GsdNaturalLight *self)
{
        return self->cached_temperature;
}

void
gsd_natural_light_set_geoclue_enabled (GsdNaturalLight *self, gboolean enabled)
{
        self->geoclue_enabled = enabled;
}

gboolean
gsd_natural_light_start (GsdNaturalLight *self, GError **error)
{

        if (self->geoclue_enabled)
                register_geoclue (self);

        natural_light_recheck (self);
        poll_timeout_create (self);

        /* care about changes */
        g_signal_connect (self->settings, "changed",
                          G_CALLBACK (settings_changed_cb), self);
        return TRUE;
}

static void
gsd_natural_light_finalize (GObject *object)
{
        GsdNaturalLight *self = GSD_NATURAL_LIGHT (object);

        poll_timeout_destroy (self);

        g_clear_object (&self->settings);
        g_clear_pointer (&self->datetime_override, (GDestroyNotify) g_date_time_unref);

        if (self->cancellable != NULL) {
                g_cancellable_cancel (self->cancellable);
                g_clear_object (&self->cancellable);
        }

        if (self->natural_light_validate_id > 0) {
                g_source_remove (self->natural_light_validate_id);
                self->natural_light_validate_id = 0;
        }

        if (self->geoclue_client != NULL)
                gclue_client_call_stop (self->geoclue_client, NULL, NULL, NULL);
        g_clear_object (&self->geoclue_simple);
        G_OBJECT_CLASS (gsd_natural_light_parent_class)->finalize (object);
}

static void
gsd_natural_light_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
        GsdNaturalLight *self = GSD_NATURAL_LIGHT (object);

        switch (prop_id) {
        case PROP_SUNRISE:
                self->cached_sunrise = g_value_get_double (value);
                break;
        case PROP_SUNSET:
                self->cached_sunset = g_value_get_double (value);
                break;
        case PROP_TEMPERATURE:
                self->cached_temperature = g_value_get_double (value);
                break;
        case PROP_DISABLED_UNTIL_TMW:
                self->disabled_until_tmw = g_value_get_boolean (value);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
}

static void
gsd_natural_light_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
        GsdNaturalLight *self = GSD_NATURAL_LIGHT (object);

        switch (prop_id) {
        case PROP_SUNRISE:
                g_value_set_double (value, self->cached_sunrise);
                break;
        case PROP_SUNSET:
                g_value_set_double (value, self->cached_sunrise);
                break;
        case PROP_TEMPERATURE:
                g_value_set_double (value, self->cached_sunrise);
                break;
        case PROP_DISABLED_UNTIL_TMW:
                g_value_set_boolean (value, gsd_natural_light_get_disabled_until_tmw (self));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
}

static void
gsd_natural_light_class_init (GsdNaturalLightClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        object_class->finalize = gsd_natural_light_finalize;

        object_class->set_property = gsd_natural_light_set_property;
        object_class->get_property = gsd_natural_light_get_property;

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

        g_object_class_install_property (object_class,
                                         PROP_TEMPERATURE,
                                         g_param_spec_double ("temperature",
                                                              "Temperature",
                                                              "Temperature in Kelvin",
                                                              GSD_COLOR_TEMPERATURE_MIN,
                                                              GSD_COLOR_TEMPERATURE_MAX,
                                                              GSD_COLOR_TEMPERATURE_DEFAULT,
                                                              G_PARAM_READWRITE));

        g_object_class_install_property (object_class,
                                         PROP_DISABLED_UNTIL_TMW,
                                         g_param_spec_boolean ("disabled-until-tmw",
                                                               "Disabled until tomorrow",
                                                               "If the natural light is disabled until the next day",
                                                               FALSE,
                                                               G_PARAM_READWRITE));

}

static void
gsd_natural_light_init (GsdNaturalLight *self)
{
        self->cached_sunrise = -1.f;
        self->cached_sunset = -1.f;
        self->cached_temperature = GSD_COLOR_TEMPERATURE_DEFAULT;
        self->cancellable = g_cancellable_new ();
        self->settings = g_settings_new ("org.gnome.settings-daemon.plugins.color");
}

GsdNaturalLight *
gsd_natural_light_new (void)
{
        return g_object_new (GSD_TYPE_NATURAL_LIGHT, NULL);
}
