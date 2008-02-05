/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gnome-settings-manager.h"
#include "gnome-settings-plugins-engine.h"

#include "gnome-settings-manager-glue.h"

#define GSD_MANAGER_DBUS_PATH "/org/gnome/SettingsDaemon"

#define GNOME_SETTINGS_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GNOME_TYPE_SETTINGS_MANAGER, GnomeSettingsManagerPrivate))

struct GnomeSettingsManagerPrivate
{
        DBusGConnection *connection;
        char            *gconf_prefix;
};

enum {
        PROP_0,
        PROP_GCONF_PREFIX,
};

static void     gnome_settings_manager_class_init  (GnomeSettingsManagerClass *klass);
static void     gnome_settings_manager_init        (GnomeSettingsManager      *settings_manager);
static void     gnome_settings_manager_finalize    (GObject                   *object);

G_DEFINE_TYPE (GnomeSettingsManager, gnome_settings_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

/*
  Example:
  dbus-send --session --dest=org.gnome.SettingsDaemon \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/gnome/SettingsDaemon \
  org.gnome.SettingsDaemon.Awake
*/
gboolean
gnome_settings_manager_awake (GnomeSettingsManager *manager,
                              GError              **error)
{
        g_debug ("Awake called");
        return TRUE;
}

static gboolean
register_manager (GnomeSettingsManager *manager)
{
        GError *error = NULL;

        error = NULL;
        manager->priv->connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
        if (manager->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
                return FALSE;
        }

        dbus_g_connection_register_g_object (manager->priv->connection, GSD_MANAGER_DBUS_PATH, G_OBJECT (manager));

        return TRUE;
}

gboolean
gnome_settings_manager_start (GnomeSettingsManager *manager,
                              GError              **error)
{
        gboolean ret;

        g_debug ("Starting settings manager");

        gnome_settings_plugins_engine_init (manager->priv->gconf_prefix);

        ret = TRUE;
        return ret;
}

void
gnome_settings_manager_stop (GnomeSettingsManager *manager)
{
        g_debug ("Stopping settings manager");

        gnome_settings_plugins_engine_shutdown ();
}

static void
_set_gconf_prefix (GnomeSettingsManager *self,
                   const char           *prefix)
{
        g_free (self->priv->gconf_prefix);
        self->priv->gconf_prefix = g_strdup (prefix);
}

static void
gnome_settings_manager_set_property (GObject        *object,
                                     guint           prop_id,
                                     const GValue   *value,
                                     GParamSpec     *pspec)
{
        GnomeSettingsManager *self;

        self = GNOME_SETTINGS_MANAGER (object);

        switch (prop_id) {
        case PROP_GCONF_PREFIX:
                _set_gconf_prefix (self, g_value_get_string (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gnome_settings_manager_get_property (GObject        *object,
                                     guint           prop_id,
                                     GValue         *value,
                                     GParamSpec     *pspec)
{
        GnomeSettingsManager *self;

        self = GNOME_SETTINGS_MANAGER (object);

        switch (prop_id) {
        case PROP_GCONF_PREFIX:
                g_value_set_string (value, self->priv->gconf_prefix);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gnome_settings_manager_constructor (GType                  type,
                                    guint                  n_construct_properties,
                                    GObjectConstructParam *construct_properties)
{
        GnomeSettingsManager      *manager;
        GnomeSettingsManagerClass *klass;

        klass = GNOME_SETTINGS_MANAGER_CLASS (g_type_class_peek (GNOME_TYPE_SETTINGS_MANAGER));

        manager = GNOME_SETTINGS_MANAGER (G_OBJECT_CLASS (gnome_settings_manager_parent_class)->constructor (type,
                                                                                                         n_construct_properties,
                                                                                                         construct_properties));

        return G_OBJECT (manager);
}

static void
gnome_settings_manager_dispose (GObject *object)
{
        GnomeSettingsManager *manager;

        manager = GNOME_SETTINGS_MANAGER (object);

        gnome_settings_manager_stop (manager);

        G_OBJECT_CLASS (gnome_settings_manager_parent_class)->dispose (object);
}

static void
gnome_settings_manager_class_init (GnomeSettingsManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gnome_settings_manager_get_property;
        object_class->set_property = gnome_settings_manager_set_property;
        object_class->constructor = gnome_settings_manager_constructor;
        object_class->dispose = gnome_settings_manager_dispose;
        object_class->finalize = gnome_settings_manager_finalize;

        g_object_class_install_property (object_class,
                                         PROP_GCONF_PREFIX,
                                         g_param_spec_string ("gconf-prefix",
                                                              "gconf-prefix",
                                                              "gconf-prefix",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (GnomeSettingsManagerPrivate));

        dbus_g_object_type_install_info (GNOME_TYPE_SETTINGS_MANAGER, &dbus_glib_gnome_settings_manager_object_info);
}

static void
gnome_settings_manager_init (GnomeSettingsManager *manager)
{

        manager->priv = GNOME_SETTINGS_MANAGER_GET_PRIVATE (manager);
}

static void
gnome_settings_manager_finalize (GObject *object)
{
        GnomeSettingsManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GNOME_IS_SETTINGS_MANAGER (object));

        manager = GNOME_SETTINGS_MANAGER (object);

        g_return_if_fail (manager->priv != NULL);

        g_free (manager->priv->gconf_prefix);

        G_OBJECT_CLASS (gnome_settings_manager_parent_class)->finalize (object);
}

GnomeSettingsManager *
gnome_settings_manager_new (const char *gconf_prefix)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                gboolean res;

                manager_object = g_object_new (GNOME_TYPE_SETTINGS_MANAGER,
                                               "gconf-prefix", gconf_prefix,
                                               NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
                res = register_manager (manager_object);
                if (! res) {
                        g_object_unref (manager_object);
                        return NULL;
                }
        }

        return GNOME_SETTINGS_MANAGER (manager_object);
}
