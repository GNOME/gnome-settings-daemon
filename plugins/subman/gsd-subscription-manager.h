/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
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

#ifndef __GSD_SUBSCRIPTION_MANAGER_H
#define __GSD_SUBSCRIPTION_MANAGER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GSD_TYPE_SUBSCRIPTION_MANAGER		(gsd_subscription_manager_get_type ())
#define GSD_SUBSCRIPTION_MANAGER(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GSD_TYPE_SUBSCRIPTION_MANAGER, GsdSubscriptionManager))
#define GSD_SUBSCRIPTION_MANAGER_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GSD_TYPE_SUBSCRIPTION_MANAGER, GsdSubscriptionManagerClass))
#define GSD_IS_SUBSCRIPTION_MANAGER(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GSD_TYPE_SUBSCRIPTION_MANAGER))
#define GSD_IS_SUBSCRIPTION_MANAGER_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GSD_TYPE_SUBSCRIPTION_MANAGER))
#define GSD_SUBSCRIPTION_MANAGER_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GSD_TYPE_SUBSCRIPTION_MANAGER, GsdSubscriptionManagerClass))
#define GSD_SUBSCRIPTION_MANAGER_ERROR		(gsd_subscription_manager_error_quark ())

typedef struct GsdSubscriptionManagerPrivate GsdSubscriptionManagerPrivate;

typedef struct
{
	GObject				 parent;
	GsdSubscriptionManagerPrivate	*priv;
} GsdSubscriptionManager;

typedef struct
{
	GObjectClass			parent_class;
} GsdSubscriptionManagerClass;

enum
{
	GSD_SUBSCRIPTION_MANAGER_ERROR_FAILED
};

GType			gsd_subscription_manager_get_type       (void);
GQuark			gsd_subscription_manager_error_quark    (void);

GsdSubscriptionManager *gsd_subscription_manager_new		(void);
gboolean		gsd_subscription_manager_start		(GsdSubscriptionManager *manager,
								 GError                **error);
void			gsd_subscription_manager_stop		(GsdSubscriptionManager *manager);

G_END_DECLS

#endif /* __GSD_SUBSCRIPTION_MANAGER_H */
