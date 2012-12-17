/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Bastien Nocera <hadess@hadess.net>
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

#ifndef __GSD_REMOTE_DISPLAY_MANAGER_H
#define __GSD_REMOTE_DISPLAY_MANAGER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GSD_TYPE_REMOTE_DISPLAY_MANAGER         (gsd_remote_display_manager_get_type ())
#define GSD_REMOTE_DISPLAY_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GSD_TYPE_REMOTE_DISPLAY_MANAGER, GsdRemoteDisplayManager))
#define GSD_REMOTE_DISPLAY_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GSD_TYPE_REMOTE_DISPLAY_MANAGER, GsdRemoteDisplayManagerClass))
#define GSD_IS_REMOTE_DISPLAY_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSD_TYPE_REMOTE_DISPLAY_MANAGER))
#define GSD_IS_REMOTE_DISPLAY_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GSD_TYPE_REMOTE_DISPLAY_MANAGER))
#define GSD_REMOTE_DISPLAY_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GSD_TYPE_REMOTE_DISPLAY_MANAGER, GsdRemoteDisplayManagerClass))

typedef struct GsdRemoteDisplayManagerPrivate GsdRemoteDisplayManagerPrivate;

typedef struct
{
        GObject                     parent;
        GsdRemoteDisplayManagerPrivate *priv;
} GsdRemoteDisplayManager;

typedef struct
{
        GObjectClass   parent_class;
} GsdRemoteDisplayManagerClass;

GType                       gsd_remote_display_manager_get_type            (void);

GsdRemoteDisplayManager *gsd_remote_display_manager_new                 (void);
gboolean                    gsd_remote_display_manager_start               (GsdRemoteDisplayManager  *manager,
                                                                               GError                     **error);
void                        gsd_remote_display_manager_stop                (GsdRemoteDisplayManager  *manager);

G_END_DECLS

#endif /* __GSD_REMOTE_DISPLAY_MANAGER_H */
