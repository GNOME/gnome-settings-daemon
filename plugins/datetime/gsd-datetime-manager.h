/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Kalev Lember <kalevlember@gmail.com>
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

#ifndef __GSD_DATETIME_MANAGER_H
#define __GSD_DATETIME_MANAGER_H

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GSD_TYPE_DATETIME_MANAGER         (gsd_datetime_manager_get_type ())
#define GSD_DATETIME_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GSD_TYPE_DATETIME_MANAGER, GsdDatetimeManager))
#define GSD_DATETIME_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GSD_TYPE_DATETIME_MANAGER, GsdDatetimeManagerClass))
#define GSD_IS_DATETIME_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSD_TYPE_DATETIME_MANAGER))
#define GSD_IS_DATETIME_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GSD_TYPE_DATETIME_MANAGER))
#define GSD_DATETIME_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GSD_TYPE_DATETIME_MANAGER, GsdDatetimeManagerClass))

typedef struct GsdDatetimeManagerPrivate GsdDatetimeManagerPrivate;

typedef struct
{
        GObject parent;
        GsdDatetimeManagerPrivate *priv;
} GsdDatetimeManager;

typedef struct
{
        GObjectClass parent_class;
} GsdDatetimeManagerClass;

GType gsd_datetime_manager_get_type (void) G_GNUC_CONST;

GsdDatetimeManager *gsd_datetime_manager_new (void);
gboolean gsd_datetime_manager_start (GsdDatetimeManager *manager, GError **error);
void gsd_datetime_manager_stop (GsdDatetimeManager *manager);

G_END_DECLS

#endif /* __GSD_DATETIME_MANAGER_H */
