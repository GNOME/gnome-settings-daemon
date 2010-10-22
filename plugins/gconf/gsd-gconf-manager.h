/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Rodrigo Moya <rodrigo@gnome.org>
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

#ifndef __GSD_GCONF_MANAGER_H
#define __GSD_GCONF_MANAGER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GSD_TYPE_GCONF_MANAGER         (gsd_gconf_manager_get_type ())
#define GSD_GCONF_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GSD_TYPE_GCONF_MANAGER, GsdGconfManager))
#define GSD_GCONF_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GSD_TYPE_GCONF_MANAGER, GsdGconfManagerClass))
#define GSD_IS_GCONF_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSD_TYPE_GCONF_MANAGER))
#define GSD_IS_GCONF_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GSD_TYPE_GCONF_MANAGER))
#define GSD_GCONF_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GSD_TYPE_GCONF_MANAGER, GsdGconfManagerClass))

typedef struct GsdGconfManagerPrivate GsdGconfManagerPrivate;

typedef struct
{
        GObject                 parent;
        GsdGconfManagerPrivate *priv;
} GsdGconfManager;

typedef struct
{
        GObjectClass   parent_class;
} GsdGconfManagerClass;

GType                   gsd_gconf_manager_get_type            (void);

GsdGconfManager *       gsd_gconf_manager_new                 (void);
gboolean                gsd_gconf_manager_start               (GsdGconfManager *manager,
                                                               GError         **error);
void                    gsd_gconf_manager_stop                (GsdGconfManager *manager);

G_END_DECLS

#endif /* __GSD_GCONF_MANAGER_H */
