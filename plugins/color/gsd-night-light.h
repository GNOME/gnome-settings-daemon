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

#ifndef __GSD_NIGHT_LIGHT_H__
#define __GSD_NIGHT_LIGHT_H__

#include <gio/gio.h>

G_BEGIN_DECLS

#define GSD_TYPE_NIGHT_LIGHT (gsd_night_light_get_type ())
G_DECLARE_FINAL_TYPE (GsdNightLight, gsd_night_light, GSD, NIGHT_LIGHT, GObject)

GsdNightLight   *gsd_night_light_new                    (void);
gboolean         gsd_night_light_start                  (GsdNightLight *self,
                                                         GError       **error);

gboolean         gsd_night_light_get_active             (GsdNightLight *self);
gdouble          gsd_night_light_get_sunrise            (GsdNightLight *self);
gdouble          gsd_night_light_get_sunset             (GsdNightLight *self);
gdouble          gsd_night_light_get_temperature        (GsdNightLight *self);

gboolean         gsd_night_light_get_disabled_until_tmw (GsdNightLight *self);
void             gsd_night_light_set_disabled_until_tmw (GsdNightLight *self,
                                                         gboolean       value);

gboolean         gsd_night_light_get_forced             (GsdNightLight *self);
void             gsd_night_light_set_forced             (GsdNightLight *self,
                                                         gboolean       value);

/* only for the self test program */
void             gsd_night_light_set_geoclue_enabled    (GsdNightLight *self,
                                                         gboolean       enabled);
void             gsd_night_light_set_date_time_now      (GsdNightLight *self,
                                                         GDateTime     *datetime);
void             gsd_night_light_set_smooth_enabled     (GsdNightLight *self,
                                                         gboolean       smooth_enabled);

G_END_DECLS

#endif
