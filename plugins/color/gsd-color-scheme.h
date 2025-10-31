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

#ifndef __GSD_COLOR_SCHEME_H__
#define __GSD_COLOR_SCHEME_H__

#include <gio/gio.h>

#include "gsd-location-monitor.h"

G_BEGIN_DECLS

#define GSD_TYPE_COLOR_SCHEME (gsd_color_scheme_get_type ())
G_DECLARE_FINAL_TYPE (GsdColorScheme, gsd_color_scheme, GSD, COLOR_SCHEME, GObject)

GsdColorScheme     *gsd_color_scheme_new                    (void);
gboolean            gsd_color_scheme_start                  (GsdColorScheme      *self,
                                                             GError             **error);

void                gsd_color_scheme_recheck_schedule       (GsdColorScheme *self);
void                gsd_color_scheme_recheck_immediate      (GsdColorScheme *self);

G_END_DECLS

#endif /* __GSD_COLOR_SCHEME_H__ */