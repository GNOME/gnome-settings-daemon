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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#ifndef __GSD_COLOR_X11_H
#define __GSD_COLOR_X11_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GSD_TYPE_COLOR_X11         (gsd_color_x11_get_type ())
#define GSD_COLOR_X11(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GSD_TYPE_COLOR_X11, GsdColorX11))
#define GSD_IS_COLOR_X11(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSD_TYPE_COLOR_X11))

typedef struct GsdColorX11Private GsdColorX11Private;

typedef struct
{
        GObject                     parent;
        GsdColorX11Private *priv;
} GsdColorX11;

typedef struct
{
        GObjectClass   parent_class;
} GsdColorX11Class;

GType                   gsd_color_x11_get_type          (void);
GQuark                  gsd_color_x11_error_quark       (void);

GsdColorX11 *           gsd_color_x11_new               (void);
gboolean                gsd_color_x11_start             (GsdColorX11 *x11,
                                                         GError     **error);

G_END_DECLS

#endif /* __GSD_COLOR_X11_H */
