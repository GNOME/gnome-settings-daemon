/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
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

#ifndef __GSD_LOCATION_MONITOR_H__
#define __GSD_LOCATION_MONITOR_H__

#include <gio/gio.h>

G_BEGIN_DECLS

#define GSD_TYPE_LOCATION_MONITOR (gsd_location_monitor_get_type ())
G_DECLARE_FINAL_TYPE (GsdLocationMonitor, gsd_location_monitor, GSD, LOCATION_MONITOR, GObject)

GsdLocationMonitor  *gsd_location_monitor_get                   (void);
gboolean             gsd_location_monitor_start                 (GsdLocationMonitor  *self,
                                                                 GError             **error);

GDateTime           *gsd_location_monitor_get_date_time_now     (GsdLocationMonitor  *self);

gdouble              gsd_location_monitor_get_sunrise           (GsdLocationMonitor  *self);
gdouble              gsd_location_monitor_get_sunset            (GsdLocationMonitor  *self);
 
/* only for the self test program */
void                 gsd_location_monitor_set_geoclue_enabled   (GsdLocationMonitor  *self,
                                                                 gboolean             enabled);
void                 gsd_location_monitor_set_date_time_now     (GsdLocationMonitor  *self,
                                                                 GDateTime           *datetime);

G_END_DECLS

#endif /* __GSD_LOCATION_MONITOR_H__ */