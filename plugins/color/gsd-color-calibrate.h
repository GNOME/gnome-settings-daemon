/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2011-2013 Richard Hughes <richard@hughsie.com>
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
 *
 */

#ifndef __GSD_COLOR_CALIBRATE_H
#define __GSD_COLOR_CALIBRATE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GSD_TYPE_COLOR_CALIBRATE         (gsd_color_calibrate_get_type ())
#define GSD_COLOR_CALIBRATE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GSD_TYPE_COLOR_CALIBRATE, GsdColorCalibrate))
#define GSD_IS_COLOR_CALIBRATE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSD_TYPE_COLOR_CALIBRATE))

typedef struct GsdColorCalibratePrivate GsdColorCalibratePrivate;

typedef struct
{
        GObject                   parent;
        GsdColorCalibratePrivate *priv;
} GsdColorCalibrate;

typedef struct
{
        GObjectClass    parent_class;
} GsdColorCalibrateClass;

GType                   gsd_color_calibrate_get_type            (void);
GQuark                  gsd_color_calibrate_error_quark         (void);

GsdColorCalibrate *     gsd_color_calibrate_new                 (void);

G_END_DECLS

#endif /* __GSD_COLOR_CALIBRATE_H */
