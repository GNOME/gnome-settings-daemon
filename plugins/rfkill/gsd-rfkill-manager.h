/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2010 Red Hat, Inc.
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

#ifndef __GSD_RFKILL_MANAGER_H
#define __GSD_RFKILL_MANAGER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GSD_TYPE_RFKILL_MANAGER         (gsd_rfkill_manager_get_type ())
#define GSD_RFKILL_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GSD_TYPE_RFKILL_MANAGER, GsdRfkillManager))
#define GSD_RFKILL_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GSD_TYPE_RFKILL_MANAGER, GsdRfkillManagerClass))
#define GSD_IS_RFKILL_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSD_TYPE_RFKILL_MANAGER))
#define GSD_IS_RFKILL_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GSD_TYPE_RFKILL_MANAGER))
#define GSD_RFKILL_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GSD_TYPE_RFKILL_MANAGER, GsdRfkillManagerClass))

typedef struct GsdRfkillManagerPrivate GsdRfkillManagerPrivate;

typedef struct
{
        GObject                     parent;
        GsdRfkillManagerPrivate *priv;
} GsdRfkillManager;

typedef struct
{
        GObjectClass   parent_class;
} GsdRfkillManagerClass;

GType                   gsd_rfkill_manager_get_type            (void);

GsdRfkillManager *       gsd_rfkill_manager_new                 (void);
gboolean                gsd_rfkill_manager_start               (GsdRfkillManager *manager,
                                                               GError         **error);
void                    gsd_rfkill_manager_stop                (GsdRfkillManager *manager);

G_END_DECLS

#endif /* __GSD_RFKILL_MANAGER_H */
