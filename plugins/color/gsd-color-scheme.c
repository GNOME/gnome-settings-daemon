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

#include "gsd-color-scheme.h"
#include "gsd-night-light-common.h"
#include "gsd-location-monitor.h"

struct _GsdColorScheme {
    GObject             parent;

    GSettings          *settings;
    GSettings          *interface_settings;

    guint               validate_id;

    GsdLocationMonitor *monitor;
};

enum {
    SCHEME_SWITCHED,
    SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0, };

#define GSD_COLOR_SCHEME_SCHEDULE_TIMEOUT      5       /* seconds */

G_DEFINE_TYPE (GsdColorScheme, gsd_color_scheme, G_TYPE_OBJECT);

static void color_scheme_recheck (GsdColorScheme *self);

void
gsd_color_scheme_recheck_immediate (GsdColorScheme *self)
{
    color_scheme_recheck (self);
}

static void
set_scheme (GsdColorScheme *self,
            gboolean        scheme_is_dark)
{
    GDesktopColorScheme system_scheme;
    GDesktopColorScheme new_scheme;

    system_scheme = g_settings_get_enum (self->interface_settings, "color-scheme");

    if (scheme_is_dark)
        new_scheme = G_DESKTOP_COLOR_SCHEME_PREFER_DARK;
    else
        new_scheme = G_DESKTOP_COLOR_SCHEME_DEFAULT;

    /* Don't needlessly transition */
    if (system_scheme == new_scheme)
        return;

    g_debug ("Changing scheme from %i to %i (0 = light, 1 = dark)", system_scheme, new_scheme);

    g_signal_emit (G_OBJECT (self), signals[SCHEME_SWITCHED], 0);

    g_settings_set_enum (self->interface_settings, "color-scheme", new_scheme);
}

static void
color_scheme_recheck (GsdColorScheme *self)
{
    gdouble frac_day;
    gdouble schedule_from = -1.f;
    gdouble schedule_to = -1.f;
    g_autoptr(GDateTime) dt_now = gsd_location_monitor_get_date_time_now (self->monitor);

    /* enabled */
    if (!g_settings_get_boolean (self->settings, "color-scheme-enabled")) {
        g_debug ("color scheme disabled");
        return;
    }

    /* calculate the position of the sun */
    if (g_settings_get_boolean (self->settings, "color-scheme-schedule-automatic")) {
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
                                               "color-scheme-schedule-from");
        schedule_to = g_settings_get_double (self->settings,
                                             "color-scheme-schedule-to");
    }

    /* get the current hour of a day as a fraction */
    frac_day = gsd_night_light_frac_day_from_dt (dt_now);
    g_debug ("fractional day = %.3f, limits = %.3f->%.3f",
             frac_day, schedule_from, schedule_to);

    if (!gsd_night_light_frac_day_is_between (frac_day,
                                           schedule_from,
                                           schedule_to)) {
        set_scheme (self, FALSE);
        return;
    }

    set_scheme (self, TRUE);
}

static gboolean
color_scheme_recheck_schedule_cb (gpointer user_data)
{
    GsdColorScheme *self = GSD_COLOR_SCHEME (user_data);
    color_scheme_recheck (self);
    self->validate_id = 0;
    return G_SOURCE_REMOVE;
}

/* called when location monitor updates */
void
gsd_color_scheme_recheck_schedule (GsdColorScheme *self)
{
    if (self->validate_id != 0)
        g_source_remove (self->validate_id);
    self->validate_id =
        g_timeout_add_seconds (GSD_COLOR_SCHEME_SCHEDULE_TIMEOUT,
                               color_scheme_recheck_schedule_cb,
                               self);
}

static void
settings_changed_cb (GSettings *settings, gchar *key, gpointer user_data)
{
    GsdColorScheme *self = GSD_COLOR_SCHEME (user_data);
    g_debug ("settings changed");

    if (g_settings_get_boolean (self->settings, "color-scheme-schedule-automatic"))
        g_signal_connect (G_OBJECT (self->monitor), "changed", G_CALLBACK (color_scheme_recheck), self);
    else
        g_signal_handlers_disconnect_by_data (self->monitor, self);

    color_scheme_recheck (self);
}

gboolean
gsd_color_scheme_start (GsdColorScheme *self, GError **error)
{
    color_scheme_recheck (self);

    /* care about changes */
    g_signal_connect (self->settings, "changed",
                      G_CALLBACK (settings_changed_cb), self);

    return TRUE;
}

static void
gsd_color_scheme_finalize (GObject *object)
{
    GsdColorScheme *self = GSD_COLOR_SCHEME (object);

    g_clear_object (&self->settings);
    g_clear_object (&self->interface_settings);
    g_clear_object (&self->monitor);

    G_OBJECT_CLASS (gsd_color_scheme_parent_class)->finalize (object);
}

static void
gsd_color_scheme_class_init (GsdColorSchemeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = gsd_color_scheme_finalize;

    signals[SCHEME_SWITCHED] =
        g_signal_new ("scheme-switched",
                      G_TYPE_FROM_CLASS (object_class),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
gsd_color_scheme_init (GsdColorScheme *self)
{
    self->settings = g_settings_new ("org.gnome.settings-daemon.plugins.color");
    self->interface_settings = g_settings_new ("org.gnome.desktop.interface");
    self->monitor = gsd_location_monitor_get ();
}

GsdColorScheme *
gsd_color_scheme_new ()
{
    GsdColorScheme *self;
    
    self = g_object_new (GSD_TYPE_COLOR_SCHEME, NULL);

    return self;
}