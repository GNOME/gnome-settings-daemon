/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Red Hat, Inc.
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

#ifndef __GSD_ACCOUNT_MANAGER_H
#define __GSD_ACCOUNT_MANAGER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GSD_TYPE_ACCOUNT_MANAGER         (gsd_account_manager_get_type ())
#define GSD_ACCOUNT_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GSD_TYPE_ACCOUNT_MANAGER, GsdAccountManager))
#define GSD_ACCOUNT_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GSD_TYPE_ACCOUNT_MANAGER, GsdAccountManagerClass))
#define GSD_IS_ACCOUNT_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSD_TYPE_ACCOUNT_MANAGER))
#define GSD_IS_ACCOUNT_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GSD_TYPE_ACCOUNT_MANAGER))
#define GSD_ACCOUNT_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GSD_TYPE_ACCOUNT_MANAGER, GsdAccountManagerClass))

typedef struct GsdAccountManagerPrivate GsdAccountManagerPrivate;

typedef struct {
        GObject                   parent;
        GsdAccountManagerPrivate *priv;
} GsdAccountManager;

typedef struct {
        GObjectClass   parent_class;
} GsdAccountManagerClass;

GType               gsd_account_manager_get_type      (void);

GsdAccountManager * gsd_account_manager_new           (void);
gboolean            gsd_account_manager_start         (GsdAccountManager  *manager,
                                                       GError            **error);
void                gsd_account_manager_stop          (GsdAccountManager  *manager);

G_END_DECLS

#endif /* __GSD_ACCOUNT_MANAGER_H */
