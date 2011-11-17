/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Red Hat, Inc
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

#include <ibus.h>

#include "gnome-settings-profile.h"
#include "gsd-ibus-manager.h"

#define IBUS_SETTINGS "desktop.ibus.shortcuts"
#define NEXT_INPUT_SOURCE_KEY "next-input-source"
#define PREV_INPUT_SOURCE_KEY "previous-input-source"

#define GSD_IBUS_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_IBUS_MANAGER, GsdIBusManagerPrivate))

struct GsdIBusManagerPrivate
{
        GSettings *ibus_settings;
};

enum {
        PROP_0,
};

static void     gsd_ibus_manager_class_init  (GsdIBusManagerClass *klass);
static void     gsd_ibus_manager_init        (GsdIBusManager      *ibus_manager);
static void     gsd_ibus_manager_finalize    (GObject             *object);

G_DEFINE_TYPE (GsdIBusManager, gsd_ibus_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static void
set_ibus_shortcut (GsdIBusManager *manager,
                   const gchar    *key,
                   const gchar    *value)
{
        IBusBus *bus;
        IBusConfig *config;
        GVariant *v;
        const gchar *str[2];

        bus = ibus_bus_new ();
        config = ibus_bus_get_config (bus);

        str[0] = value;
        str[1] = NULL;

        v = g_variant_new_strv (str, 1);

        if (g_str_equal (key, NEXT_INPUT_SOURCE_KEY)) {
                ibus_config_set_value (config, "general/hotkey", "next_engine_in_menu", v);
        }
        else if (g_str_equal (key, PREV_INPUT_SOURCE_KEY)) {
                ibus_config_set_value (config, "general/hotkey", "previous_engine", v);
        }

        g_variant_unref (v);

        g_object_unref (bus);
}

static void
ibus_settings_changed (GSettings      *settings,
                       const gchar    *key,
                       GsdIBusManager *manager)
{
        if (g_str_equal (key, NEXT_INPUT_SOURCE_KEY) ||
            g_str_equal (key, PREV_INPUT_SOURCE_KEY)) {
                gchar *value;

                value = g_settings_get_string (settings, key);
                set_ibus_shortcut (manager, key, value);
                g_free (value);
        }
}

gboolean
gsd_ibus_manager_start (GsdIBusManager  *manager,
                        GError         **error)
{
        g_debug ("Starting ibus manager");
        gnome_settings_profile_start (NULL);

        ibus_init ();

        manager->priv->ibus_settings = g_settings_new (IBUS_SETTINGS);
        g_signal_connect (manager->priv->ibus_settings, "changed",
                          G_CALLBACK (ibus_settings_changed), manager);

        gnome_settings_profile_end (NULL);
        return TRUE;
}

void
gsd_ibus_manager_stop (GsdIBusManager *manager)
{
        g_debug ("Stopping ibus manager");

        g_object_unref (manager->priv->ibus_settings);
        manager->priv->ibus_settings = NULL;
}

static void
gsd_ibus_manager_set_property (GObject        *object,
                               guint           prop_id,
                               const GValue   *value,
                               GParamSpec     *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsd_ibus_manager_get_property (GObject        *object,
                               guint           prop_id,
                               GValue         *value,
                               GParamSpec     *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gsd_ibus_manager_constructor (GType                  type,
                              guint                  n_construct_properties,
                              GObjectConstructParam *construct_properties)
{
        GsdIBusManager      *ibus_manager;

        ibus_manager = GSD_IBUS_MANAGER (G_OBJECT_CLASS (gsd_ibus_manager_parent_class)->constructor (type,
                                                                                                      n_construct_properties,
                                                                                                      construct_properties));

        return G_OBJECT (ibus_manager);
}

static void
gsd_ibus_manager_dispose (GObject *object)
{
        G_OBJECT_CLASS (gsd_ibus_manager_parent_class)->dispose (object);
}

static void
gsd_ibus_manager_class_init (GsdIBusManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsd_ibus_manager_get_property;
        object_class->set_property = gsd_ibus_manager_set_property;
        object_class->constructor = gsd_ibus_manager_constructor;
        object_class->dispose = gsd_ibus_manager_dispose;
        object_class->finalize = gsd_ibus_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdIBusManagerPrivate));
}

static void
gsd_ibus_manager_init (GsdIBusManager *manager)
{
        manager->priv = GSD_IBUS_MANAGER_GET_PRIVATE (manager);

}

static void
gsd_ibus_manager_finalize (GObject *object)
{
        GsdIBusManager *ibus_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_IBUS_MANAGER (object));

        ibus_manager = GSD_IBUS_MANAGER (object);

        g_return_if_fail (ibus_manager->priv != NULL);

        G_OBJECT_CLASS (gsd_ibus_manager_parent_class)->finalize (object);
}

GsdIBusManager *
gsd_ibus_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_IBUS_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_IBUS_MANAGER (manager_object);
}
