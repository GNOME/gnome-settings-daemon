/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Rodrigo Moya <rodrigo@gnome.org>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"
#include "gsd-gconf-manager.h"

#define GSD_GCONF_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_GCONF_MANAGER, GsdGconfManagerPrivate))

struct GsdGconfManagerPrivate {
};

GsdGconfManager *manager_object = NULL;

G_DEFINE_TYPE(GsdGconfManager, gsd_gconf_manager, G_TYPE_OBJECT)

static void
gsd_gconf_manager_finalize (GObject *object)
{
	GsdGconfManager *manager = GSD_GCONF_MANAGER (object);

	g_return_if_fail (manager->priv != NULL);

	G_OBJECT_CLASS (gsd_gconf_manager_parent_class)->finalize (object);
}

static void
gsd_gconf_manager_class_init (GsdGconfManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = gsd_gconf_manager_finalize;

	g_type_class_add_private (klass, sizeof (GsdGconfManagerPrivate));
}

static void
gsd_gconf_manager_init (GsdGconfManager *manager)
{
	manager->priv = GSD_GCONF_MANAGER_GET_PRIVATE (manager);
}

GsdGconfManager *
gsd_gconf_manager_new (void)
{
	if (manager_object != NULL) {
		g_object_ref (manager_object);
	} else {
		manager_object = g_object_new (GSD_TYPE_GCONF_MANAGER, NULL);
		g_object_add_weak_pointer (manager_object,
					   (gpointer *) &manager_object);
	}

	return manager_object;
}

gboolean
gsd_gconf_manager_start (GsdGconfManager *manager, GError **error)
{
}

void
gsd_gconf_manager_stop (GsdGconfManager *manager)
{
}
