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

#ifndef __GSD_SHARING_MANAGER_H
#define __GSD_SHARING_MANAGER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GSD_TYPE_SHARING_MANAGER         (gsd_sharing_manager_get_type ())
#define GSD_SHARING_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GSD_TYPE_SHARING_MANAGER, GsdSharingManager))
#define GSD_SHARING_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GSD_TYPE_SHARING_MANAGER, GsdSharingManagerClass))
#define GSD_IS_SHARING_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSD_TYPE_SHARING_MANAGER))
#define GSD_IS_SHARING_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GSD_TYPE_SHARING_MANAGER))
#define GSD_SHARING_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GSD_TYPE_SHARING_MANAGER, GsdSharingManagerClass))

typedef struct GsdSharingManagerPrivate GsdSharingManagerPrivate;

typedef struct
{
        GObject                     parent;
        GsdSharingManagerPrivate *priv;
} GsdSharingManager;

typedef struct
{
        GObjectClass   parent_class;
} GsdSharingManagerClass;

GType                   gsd_sharing_manager_get_type            (void);

GsdSharingManager *       gsd_sharing_manager_new                 (void);
gboolean                gsd_sharing_manager_start               (GsdSharingManager *manager,
                                                               GError         **error);
void                    gsd_sharing_manager_stop                (GsdSharingManager *manager);

G_END_DECLS

#endif /* __GSD_SHARING_MANAGER_H */
