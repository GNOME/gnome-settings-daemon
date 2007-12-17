/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright © 2001 Ximian, Inc.
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
#include <gconf/gconf-client.h>

#include "gsd-background-manager.h"

#include "preferences.h"
#include "applier.h"

#define GSD_BACKGROUND_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_BACKGROUND_MANAGER, GsdBackgroundManagerPrivate))

struct GsdBackgroundManagerPrivate
{
        BGApplier    **bg_appliers;
        BGPreferences *prefs;
        guint          applier_idle_id;
};

enum {
        PROP_0,
};

static void     gsd_background_manager_class_init  (GsdBackgroundManagerClass *klass);
static void     gsd_background_manager_init        (GsdBackgroundManager      *background_manager);
static void     gsd_background_manager_finalize    (GObject             *object);

G_DEFINE_TYPE (GsdBackgroundManager, gsd_background_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static gboolean
applier_idle (GsdBackgroundManager *manager)
{
        int i;

        for (i = 0; manager->priv->bg_appliers [i]; i++) {
                bg_applier_apply_prefs (manager->priv->bg_appliers [i], manager->priv->prefs);
        }
        manager->priv->applier_idle_id = 0;
        return FALSE;
}

static void
background_callback (GConfClient          *client,
                     guint                 cnxn_id,
                     GConfEntry           *entry,
                     GsdBackgroundManager *manager)
{
        bg_preferences_merge_entry (manager->priv->prefs, entry);

        if (manager->priv->applier_idle_id != 0) {
                g_source_remove (manager->priv->applier_idle_id);
        }

        manager->priv->applier_idle_id = g_timeout_add (100, (GSourceFunc)applier_idle, manager);
}

gboolean
gsd_background_manager_start (GsdBackgroundManager *manager,
                              GError              **error)
{
        GdkDisplay  *display;
        int          n_screens;
        int          i;
        GConfClient *client;

        g_debug ("Starting background manager");

        display = gdk_display_get_default ();
        n_screens = gdk_display_get_n_screens (display);

        manager->priv->bg_appliers = g_new (BGApplier *, n_screens + 1);

        for (i = 0; i < n_screens; i++) {
                GdkScreen *screen;

                screen = gdk_display_get_screen (display, i);

                manager->priv->bg_appliers [i] = BG_APPLIER (bg_applier_new_for_screen (BG_APPLIER_ROOT, screen));
        }

        manager->priv->bg_appliers [i] = NULL;

        manager->priv->prefs = BG_PREFERENCES (bg_preferences_new ());
        bg_preferences_load (manager->priv->prefs);

        client = gconf_client_get_default ();
        gconf_client_notify_add (client,
                                 "/desktop/gnome/background",
                                 (GConfClientNotifyFunc)background_callback,
                                 manager,
                                 NULL,
                                 NULL);
        g_object_unref (client);

        return TRUE;
}

void
gsd_background_manager_stop (GsdBackgroundManager *manager)
{
        g_debug ("Stopping background manager");
}

static void
gsd_background_manager_set_property (GObject        *object,
                               guint           prop_id,
                               const GValue   *value,
                               GParamSpec     *pspec)
{
        GsdBackgroundManager *self;

        self = GSD_BACKGROUND_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsd_background_manager_get_property (GObject        *object,
                               guint           prop_id,
                               GValue         *value,
                               GParamSpec     *pspec)
{
        GsdBackgroundManager *self;

        self = GSD_BACKGROUND_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gsd_background_manager_constructor (GType                  type,
                              guint                  n_construct_properties,
                              GObjectConstructParam *construct_properties)
{
        GsdBackgroundManager      *background_manager;
        GsdBackgroundManagerClass *klass;

        klass = GSD_BACKGROUND_MANAGER_CLASS (g_type_class_peek (GSD_TYPE_BACKGROUND_MANAGER));

        background_manager = GSD_BACKGROUND_MANAGER (G_OBJECT_CLASS (gsd_background_manager_parent_class)->constructor (type,
                                                                                                      n_construct_properties,
                                                                                                      construct_properties));

        return G_OBJECT (background_manager);
}

static void
gsd_background_manager_dispose (GObject *object)
{
        GsdBackgroundManager *background_manager;

        background_manager = GSD_BACKGROUND_MANAGER (object);

        G_OBJECT_CLASS (gsd_background_manager_parent_class)->dispose (object);
}

static void
gsd_background_manager_class_init (GsdBackgroundManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsd_background_manager_get_property;
        object_class->set_property = gsd_background_manager_set_property;
        object_class->constructor = gsd_background_manager_constructor;
        object_class->dispose = gsd_background_manager_dispose;
        object_class->finalize = gsd_background_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdBackgroundManagerPrivate));
}

static void
gsd_background_manager_init (GsdBackgroundManager *manager)
{
        manager->priv = GSD_BACKGROUND_MANAGER_GET_PRIVATE (manager);

}

static void
gsd_background_manager_finalize (GObject *object)
{
        GsdBackgroundManager *background_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_BACKGROUND_MANAGER (object));

        background_manager = GSD_BACKGROUND_MANAGER (object);

        g_return_if_fail (background_manager->priv != NULL);

        G_OBJECT_CLASS (gsd_background_manager_parent_class)->finalize (object);
}

GsdBackgroundManager *
gsd_background_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_BACKGROUND_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_BACKGROUND_MANAGER (manager_object);
}
