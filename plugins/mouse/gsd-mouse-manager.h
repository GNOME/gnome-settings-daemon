/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
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

#ifndef __GSD_MOUSE_MANAGER_H
#define __GSD_MOUSE_MANAGER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GSD_TYPE_MOUSE_MANAGER         (gsd_mouse_manager_get_type ())

G_DECLARE_FINAL_TYPE (GsdMouseManager, gsd_mouse_manager, GSD, MOUSE_MANAGER, GObject)

GsdMouseManager *       gsd_mouse_manager_new                 (void);
gboolean                gsd_mouse_manager_start               (GsdMouseManager *manager,
                                                               GError         **error);
void                    gsd_mouse_manager_stop                (GsdMouseManager *manager);

G_END_DECLS

#endif /* __GSD_MOUSE_MANAGER_H */
