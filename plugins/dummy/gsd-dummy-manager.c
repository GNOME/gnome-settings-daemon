/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2003 Ross Burton <ross@burtonini.com>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "gsd-xrdb-manager.h"

#define GSD_XRDB_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_XRDB_MANAGER, GsdXrdbManagerPrivate))

struct GsdXrdbManagerPrivate
{

};

enum {
        PROP_0,
};

static void     gsd_xrdb_manager_class_init  (GsdXrdbManagerClass *klass);
static void     gsd_xrdb_manager_init        (GsdXrdbManager      *xrdb_manager);
static void     gsd_xrdb_manager_finalize    (GObject             *object);

G_DEFINE_TYPE (GsdXrdbManager, gsd_xrdb_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

gboolean
gsd_xrdb_manager_start (GsdXrdbManager *manager,
                               GError               **error)
{
        GConfClient *client;
        int          i;

        g_debug ("Starting xrdb manager");

        client = gconf_client_get_default ();

        g_object_unref (client);


        return TRUE;
}

void
gsd_xrdb_manager_stop (GsdXrdbManager *manager)
{
        g_debug ("Stopping xrdb manager");
}

static void
gsd_xrdb_manager_set_property (GObject        *object,
                               guint           prop_id,
                               const GValue   *value,
                               GParamSpec     *pspec)
{
        GsdXrdbManager *self;

        self = GSD_XRDB_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsd_xrdb_manager_get_property (GObject        *object,
                               guint           prop_id,
                               GValue         *value,
                               GParamSpec     *pspec)
{
        GsdXrdbManager *self;

        self = GSD_XRDB_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gsd_xrdb_manager_constructor (GType                  type,
                              guint                  n_construct_properties,
                              GObjectConstructParam *construct_properties)
{
        GsdXrdbManager      *xrdb_manager;
        GsdXrdbManagerClass *klass;

        klass = GSD_XRDB_MANAGER_CLASS (g_type_class_peek (GSD_TYPE_XRDB_MANAGER));

        xrdb_manager = GSD_XRDB_MANAGER (G_OBJECT_CLASS (gsd_xrdb_manager_parent_class)->constructor (type,
                                                                                                      n_construct_properties,
                                                                                                      construct_properties));

        return G_OBJECT (xrdb_manager);
}

static void
gsd_xrdb_manager_dispose (GObject *object)
{
        GsdXrdbManager *xrdb_manager;

        xrdb_manager = GSD_XRDB_MANAGER (object);

        G_OBJECT_CLASS (gsd_xrdb_manager_parent_class)->dispose (object);
}

static void
gsd_xrdb_manager_class_init (GsdXrdbManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsd_xrdb_manager_get_property;
        object_class->set_property = gsd_xrdb_manager_set_property;
        object_class->constructor = gsd_xrdb_manager_constructor;
        object_class->dispose = gsd_xrdb_manager_dispose;
        object_class->finalize = gsd_xrdb_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdXrdbManagerPrivate));
}

static void
gsd_xrdb_manager_init (GsdXrdbManager *manager)
{
        manager->priv = GSD_XRDB_MANAGER_GET_PRIVATE (manager);

}

static void
gsd_xrdb_manager_finalize (GObject *object)
{
        GsdXrdbManager *xrdb_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_XRDB_MANAGER (object));

        xrdb_manager = GSD_XRDB_MANAGER (object);

        g_return_if_fail (xrdb_manager->priv != NULL);

        G_OBJECT_CLASS (gsd_xrdb_manager_parent_class)->finalize (object);
}

GsdXrdbManager *
gsd_xrdb_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_XRDB_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_XRDB_MANAGER (manager_object);
}
