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

#ifndef __GSD_NATURAL_LIGHT_H__
#define __GSD_NATURAL_LIGHT_H__

#include <gio/gio.h>

G_BEGIN_DECLS

#define GSD_TYPE_NATURAL_LIGHT (gsd_natural_light_get_type ())
G_DECLARE_FINAL_TYPE (GsdNaturalLight, gsd_natural_light, GSD, NATURAL_LIGHT, GObject)

GsdNaturalLight *gsd_natural_light_new                    (void);
gboolean         gsd_natural_light_start                  (GsdNaturalLight *self,
                                                           GError         **error);
gdouble          gsd_natural_light_get_sunrise            (GsdNaturalLight *self);
gdouble          gsd_natural_light_get_sunset             (GsdNaturalLight *self);
gdouble          gsd_natural_light_get_temperature        (GsdNaturalLight *self);

gboolean         gsd_natural_light_get_disabled_until_tmw (GsdNaturalLight *self);
void             gsd_natural_light_set_disabled_until_tmw (GsdNaturalLight *self,
                                                           gboolean         value);

/* only for the self test program */
void             gsd_natural_light_set_geoclue_enabled    (GsdNaturalLight *self,
                                                           gboolean         enabled);
void             gsd_natural_light_set_date_time_now      (GsdNaturalLight *self,
                                                           GDateTime       *datetime);

G_END_DECLS

#endif
