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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <glib-object.h>

#define GSD_TYPE_REMOTE_DISPLAY_MANAGER            (gsd_remote_display_manager_get_type ())
#define GSD_REMOTE_DISPLAY_MANAGER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSD_TYPE_REMOTE_DISPLAY_MANAGER, GsdRemoteDisplayManager))
#define GSD_IS_REMOTE_DISPLAY_MANAGER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSD_TYPE_REMOTE_DISPLAY_MANAGER))
#define GSD_REMOTE_DISPLAY_MANAGER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GSD_TYPE_REMOTE_DISPLAY_MANAGER, GsdRemoteDisplayManagerClass))
#define GSD_IS_REMOTE_DISPLAY_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GSD_TYPE_REMOTE_DISPLAY_MANAGER))
#define GSD_REMOTE_DISPLAY_MANAGER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GSD_TYPE_REMOTE_DISPLAY_MANAGER, GsdRemoteDisplayManagerClass))

typedef struct GsdRemoteDisplayManager GsdRemoteDisplayManager;
typedef struct GsdRemoteDisplayManagerClass GsdRemoteDisplayManagerClass;

GType gsd_remote_display_manager_get_type (void) G_GNUC_CONST;

GsdRemoteDisplayManager * gsd_remote_display_manager_new (void);
