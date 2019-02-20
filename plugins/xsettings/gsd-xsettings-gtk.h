/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
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

#ifndef __GSD_XSETTINGS_GTK_H__
#define __GSD_XSETTINGS_GTK_H__

#include <glib.h>
#include <glib-object.h>
#include <gmodule.h>

G_BEGIN_DECLS

#define GSD_TYPE_XSETTINGS_GTK                (gsd_xsettings_gtk_get_type ())

G_DECLARE_FINAL_TYPE (GsdXSettingsGtk, gsd_xsettings_gtk, GSD, XSETTINGS_GTK, GObject)

GsdXSettingsGtk *gsd_xsettings_gtk_new        (void);

const char * gsd_xsettings_gtk_get_modules (GsdXSettingsGtk *gtk);

G_END_DECLS

#endif /* __GSD_XSETTINGS_GTK_H__ */
