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

#include "gsd-color-state.h"

#include "gsd-night-light.h"
#include "gsd-night-light-common.h"

struct _GsdNightLight {
        GObject            parent;
        GSettings         *settings;
        gboolean           forced;
        gboolean           disabled_until_tmw;
        GDateTime         *disabled_until_tmw_dt;
        guint              validate_id;
        gdouble            cached_temperature;
        gboolean           cached_active;
        gboolean           smooth_enabled;
        GTimer            *smooth_timer;
        guint              smooth_id;
        gdouble            smooth_target_temperature;
        GCancellable      *cancellable;
        GsdLocationMonitor *monitor;
};

enum {
        PROP_0,
        PROP_ACTIVE,
        PROP_TEMPERATURE,
        PROP_DISABLED_UNTIL_TMW,
        PROP_FORCED,
        PROP_LAST
};

#define GSD_NIGHT_LIGHT_SCHEDULE_TIMEOUT      5       /* seconds */
#define GSD_NIGHT_LIGHT_POLL_TIMEOUT          60      /* seconds */
#define GSD_NIGHT_LIGHT_POLL_SMEAR            1       /* hours */
#define GSD_NIGHT_LIGHT_SMOOTH_SMEAR          5.f     /* seconds */

#define GSD_TEMPERATURE_MAX_DELTA               (10.f)          /* Kelvin */

#define DESKTOP_ID "gnome-color-panel"

static void night_light_recheck (GsdNightLight *self);

G_DEFINE_TYPE (GsdNightLight, gsd_night_light, G_TYPE_OBJECT);

void
gsd_night_light_recheck_immediate (GsdNightLight *self)
{
        night_light_recheck (self);
}

static void
poll_smooth_destroy (GsdNightLight *self)
{
        if (self->smooth_id != 0) {
                g_source_remove (self->smooth_id);
                self->smooth_id = 0;
        }
        if (self->smooth_timer != NULL)
                g_clear_pointer (&self->smooth_timer, g_timer_destroy);
}

void
gsd_night_light_set_smooth_enabled (GsdNightLight *self,
                                    gboolean smooth_enabled)
{
        /* ensure the timeout is stopped if called at runtime */
        if (!smooth_enabled)
                poll_smooth_destroy (self);
        self->smooth_enabled = smooth_enabled;
}

static gdouble
linear_interpolate (gdouble val1, gdouble val2, gdouble factor)
{
        g_return_val_if_fail (factor >= 0.f, -1.f);
        g_return_val_if_fail (factor <= 1.f, -1.f);
        return ((val1 - val2) * factor) + val2;
}

static void
gsd_night_light_set_temperature_internal (GsdNightLight *self, gdouble temperature, gboolean force)
{
        if (!force && ABS (self->cached_temperature - temperature) <= GSD_TEMPERATURE_MAX_DELTA)
                return;
        if (self->cached_temperature == temperature)
                return;
        self->cached_temperature = temperature;
        g_object_notify (G_OBJECT (self), "temperature");
}

static gboolean
gsd_night_light_smooth_cb (gpointer user_data)
{
        GsdNightLight *self = GSD_NIGHT_LIGHT (user_data);
        gdouble tmp;
        gdouble frac;

        /* find fraction */
        frac = g_timer_elapsed (self->smooth_timer, NULL) / GSD_NIGHT_LIGHT_SMOOTH_SMEAR;
        if (frac >= 1.f) {
                gsd_night_light_set_temperature_internal (self,
                                                          self->smooth_target_temperature,
                                                          TRUE);
                self->smooth_id = 0;
                return G_SOURCE_REMOVE;
        }

        /* set new temperature step using log curve */
        tmp = self->smooth_target_temperature - self->cached_temperature;
        tmp *= frac;
        tmp += self->cached_temperature;
        gsd_night_light_set_temperature_internal (self, tmp, FALSE);

        return G_SOURCE_CONTINUE;
}

static void
poll_smooth_create (GsdNightLight *self, gdouble temperature)
{
        g_assert (self->smooth_id == 0);
        self->smooth_target_temperature = temperature;
        self->smooth_timer = g_timer_new ();
        self->smooth_id = g_timeout_add (50, gsd_night_light_smooth_cb, self);
}

static void
gsd_night_light_set_temperature (GsdNightLight *self, gdouble temperature)
{
        /* immediate */
        if (!self->smooth_enabled) {
                gsd_night_light_set_temperature_internal (self, temperature, TRUE);
                return;
        }

        /* Destroy any smooth transition, it will be recreated if neccessary */
        poll_smooth_destroy (self);

        /* small jump */
        if (ABS (temperature - self->cached_temperature) < GSD_TEMPERATURE_MAX_DELTA) {
                gsd_night_light_set_temperature_internal (self, temperature, TRUE);
                return;
        }

        /* smooth out the transition */
        poll_smooth_create (self, temperature);
}

static void
gsd_night_light_set_active (GsdNightLight *self, gboolean active)
{
        if (self->cached_active == active)
                return;
        self->cached_active = active;

        /* ensure set to unity temperature */
        if (!active)
                gsd_night_light_set_temperature (self, GSD_COLOR_TEMPERATURE_DEFAULT);

        g_object_notify (G_OBJECT (self), "active");
}

static void
night_light_recheck (GsdNightLight *self)
{
        gdouble frac_day;
        gdouble schedule_from = -1.f;
        gdouble schedule_to = -1.f;
        gdouble smear = GSD_NIGHT_LIGHT_POLL_SMEAR; /* hours */
        guint temperature;
        guint temp_smeared;
        g_autoptr(GDateTime) dt_now = gsd_location_monitor_get_date_time_now (self->monitor);

        /* Forced mode, just set the temperature to night light.
         * Proper rechecking will happen once forced mode is disabled again */
        if (self->forced) {
                temperature = g_settings_get_uint (self->settings, "night-light-temperature");
                gsd_night_light_set_temperature (self, temperature);
                return;
        }

        /* enabled */
        if (!g_settings_get_boolean (self->settings, "night-light-enabled")) {
                g_debug ("night light disabled, resetting");
                gsd_night_light_set_active (self, FALSE);
                return;
        }

        /* calculate the position of the sun */
        if (g_settings_get_boolean (self->settings, "night-light-schedule-automatic")) {
                gdouble cached_sunrise = gsd_location_monitor_get_sunrise (self->monitor);
                gdouble cached_sunset = gsd_location_monitor_get_sunset (self->monitor);

                if (cached_sunrise > 0.f && cached_sunset > 0.f) {
                        schedule_to = cached_sunrise;
                        schedule_from = cached_sunset;
                }
        }

        /* fall back to manual settings */
        if (schedule_to <= 0.f || schedule_from <= 0.f) {
                schedule_from = g_settings_get_double (self->settings,
                                                       "night-light-schedule-from");
                schedule_to = g_settings_get_double (self->settings,
                                                     "night-light-schedule-to");
        }

        /* get the current hour of a day as a fraction */
        frac_day = gsd_night_light_frac_day_from_dt (dt_now);
        g_debug ("fractional day = %.3f, limits = %.3f->%.3f",
                 frac_day, schedule_from, schedule_to);

        /* disabled until tomorrow */
        if (self->disabled_until_tmw) {
                GTimeSpan time_span;
                gboolean reset = FALSE;

                time_span = g_date_time_difference (dt_now, self->disabled_until_tmw_dt);

                /* Reset if disabled until tomorrow is more than 24h ago. */
                if (time_span > (GTimeSpan) 24 * 60 * 60 * 1000000) {
                        g_debug ("night light disabled until tomorrow is older than 24h, resetting disabled until tomorrow");
                        reset = TRUE;
                } else if (time_span > 0) {
                        /* Or if a sunrise lies between the time it was disabled and now. */
                        gdouble frac_disabled;
                        frac_disabled = gsd_night_light_frac_day_from_dt (self->disabled_until_tmw_dt);
                        if (frac_disabled != frac_day &&
                            gsd_night_light_frac_day_is_between (schedule_to,
                                                                 frac_disabled,
                                                                 frac_day)) {
                                g_debug ("night light sun rise happened, resetting disabled until tomorrow");
                                reset = TRUE;
                        }
                }

                if (reset) {
                        self->disabled_until_tmw = FALSE;
                        g_clear_pointer(&self->disabled_until_tmw_dt, g_date_time_unref);
                        g_object_notify (G_OBJECT (self), "disabled-until-tmw");
                }
        }

        /* lower smearing period to be smaller than the time between start/stop */
        smear = MIN (smear,
                     MIN (     ABS (schedule_to - schedule_from),
                          24 - ABS (schedule_to - schedule_from)));

        if (!gsd_night_light_frac_day_is_between (frac_day,
                                                  schedule_from - smear,
                                                  schedule_to)) {
                g_debug ("not time for night-light");
                gsd_night_light_set_active (self, FALSE);
                return;
        }

        gsd_night_light_set_active (self, TRUE);

        if (self->disabled_until_tmw) {
                g_debug ("night light still day-disabled");
                gsd_night_light_set_temperature (self, GSD_COLOR_TEMPERATURE_DEFAULT);
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
        temperature = g_settings_get_uint (self->settings, "night-light-temperature");
        if (smear < 0.01) {
                /* Don't try to smear for extremely short or zero periods */
                temp_smeared = temperature;
        } else if (gsd_night_light_frac_day_is_between (frac_day,
                                                        schedule_from - smear,
                                                        schedule_from)) {
                gdouble factor = 1.f - ((frac_day - (schedule_from - smear)) / smear);
                temp_smeared = linear_interpolate (GSD_COLOR_TEMPERATURE_DEFAULT,
                                                   temperature, factor);
        } else if (gsd_night_light_frac_day_is_between (frac_day,
                                                        schedule_to - smear,
                                                        schedule_to)) {
                gdouble factor = (frac_day - (schedule_to - smear)) / smear;
                temp_smeared = linear_interpolate (GSD_COLOR_TEMPERATURE_DEFAULT,
                                                   temperature, factor);
        } else {
                temp_smeared = temperature;
        }

        g_debug ("night light mode on, using temperature of %uK (aiming for %uK)",
                 temp_smeared, temperature);
        gsd_night_light_set_temperature (self, temp_smeared);
}

static gboolean
night_light_recheck_schedule_cb (gpointer user_data)
{
        GsdNightLight *self = GSD_NIGHT_LIGHT (user_data);
        night_light_recheck (self);
        self->validate_id = 0;
        return G_SOURCE_REMOVE;
}

/* called when location monitor updates */
void
gsd_night_light_recheck_schedule (GsdNightLight *self)
{
        if (self->validate_id != 0)
                g_source_remove (self->validate_id);
        self->validate_id =
                g_timeout_add_seconds (GSD_NIGHT_LIGHT_SCHEDULE_TIMEOUT,
                                       night_light_recheck_schedule_cb,
                                       self);
}

static void
settings_changed_cb (GSettings *settings, gchar *key, gpointer user_data)
{
        GsdNightLight *self = GSD_NIGHT_LIGHT (user_data);
        g_debug ("settings changed");

        if (g_settings_get_boolean (self->settings, "night-light-schedule-automatic"))
                g_signal_connect (G_OBJECT (self->monitor), "changed", G_CALLBACK (night_light_recheck), self);
        else
                g_signal_handlers_disconnect_by_data (self->monitor, self);
        
        night_light_recheck (self);
}

void
gsd_night_light_set_disabled_until_tmw (GsdNightLight *self, gboolean value)
{
        g_autoptr(GDateTime) dt = gsd_location_monitor_get_date_time_now (self->monitor);

        if (self->disabled_until_tmw == value)
                return;

        self->disabled_until_tmw = value;
        g_clear_pointer (&self->disabled_until_tmw_dt, g_date_time_unref);
        if (self->disabled_until_tmw)
                self->disabled_until_tmw_dt = g_steal_pointer (&dt);
        night_light_recheck (self);
        g_object_notify (G_OBJECT (self), "disabled-until-tmw");
}

gboolean
gsd_night_light_get_disabled_until_tmw (GsdNightLight *self)
{
        return self->disabled_until_tmw;
}

void
gsd_night_light_set_forced (GsdNightLight *self, gboolean value)
{
        if (self->forced == value)
                return;

        self->forced = value;
        g_object_notify (G_OBJECT (self), "forced");

        /* A simple recheck might not reset the temperature if
         * night light is currently disabled. */
        if (!self->forced && !self->cached_active)
                gsd_night_light_set_temperature (self, GSD_COLOR_TEMPERATURE_DEFAULT);

        night_light_recheck (self);
}

gboolean
gsd_night_light_get_forced (GsdNightLight *self)
{
        return self->forced;
}

gboolean
gsd_night_light_get_active (GsdNightLight *self)
{
        return self->cached_active;
}

gdouble
gsd_night_light_get_temperature (GsdNightLight *self)
{
        return self->cached_temperature;
}

gboolean
gsd_night_light_start (GsdNightLight *self, GError **error)
{
        night_light_recheck (self);

        /* care about changes */
        g_signal_connect (self->settings, "changed",
                          G_CALLBACK (settings_changed_cb), self);

        return TRUE;
}

static void
gsd_night_light_finalize (GObject *object)
{
        GsdNightLight *self = GSD_NIGHT_LIGHT (object);

        poll_smooth_destroy (self);

        g_clear_object (&self->settings);
        g_clear_object (&self->monitor);
        g_clear_pointer (&self->disabled_until_tmw_dt, g_date_time_unref);

        if (self->validate_id > 0) {
                g_source_remove (self->validate_id);
                self->validate_id = 0;
        }

        G_OBJECT_CLASS (gsd_night_light_parent_class)->finalize (object);
}

static void
gsd_night_light_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
        GsdNightLight *self = GSD_NIGHT_LIGHT (object);

        switch (prop_id) {
        case PROP_TEMPERATURE:
                self->cached_temperature = g_value_get_double (value);
                break;
        case PROP_DISABLED_UNTIL_TMW:
                gsd_night_light_set_disabled_until_tmw (self, g_value_get_boolean (value));
                break;
        case PROP_FORCED:
                gsd_night_light_set_forced (self, g_value_get_boolean (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
}

static void
gsd_night_light_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
        GsdNightLight *self = GSD_NIGHT_LIGHT (object);

        switch (prop_id) {
        case PROP_ACTIVE:
                g_value_set_boolean (value, self->cached_active);
                break;
        case PROP_TEMPERATURE:
                g_value_set_double (value, self->cached_temperature);
                break;
        case PROP_DISABLED_UNTIL_TMW:
                g_value_set_boolean (value, gsd_night_light_get_disabled_until_tmw (self));
                break;
        case PROP_FORCED:
                g_value_set_boolean (value, gsd_night_light_get_forced (self));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
}

static void
gsd_night_light_class_init (GsdNightLightClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        object_class->finalize = gsd_night_light_finalize;

        object_class->set_property = gsd_night_light_set_property;
        object_class->get_property = gsd_night_light_get_property;

        g_object_class_install_property (object_class,
                                         PROP_ACTIVE,
                                         g_param_spec_boolean ("active",
                                                               "Active",
                                                               "If night light functionality is active right now",
                                                               FALSE,
                                                               G_PARAM_READABLE));

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
                                                               "If the night light is disabled until the next day",
                                                               FALSE,
                                                               G_PARAM_READWRITE));

        g_object_class_install_property (object_class,
                                         PROP_FORCED,
                                         g_param_spec_boolean ("forced",
                                                               "Forced",
                                                               "Whether night light should be forced on, useful for previewing",
                                                               FALSE,
                                                               G_PARAM_READWRITE));

}

static void
gsd_night_light_init (GsdNightLight *self)
{
        self->smooth_enabled = TRUE;
        self->cached_temperature = GSD_COLOR_TEMPERATURE_DEFAULT;
        self->settings = g_settings_new ("org.gnome.settings-daemon.plugins.color");
        self->monitor = gsd_location_monitor_get ();
}

GsdNightLight *
gsd_night_light_new ()
{
        GsdNightLight *self;

        self = g_object_new (GSD_TYPE_NIGHT_LIGHT, NULL);

        return self;
}
