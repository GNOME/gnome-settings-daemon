/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Kalev Lember <kalevlember@gmail.com>
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

#include <gio/gio.h>

#include "gsd-datetime-manager.h"
#include "gsd-timezone-monitor.h"
#include "gnome-settings-profile.h"

#define GSD_DATETIME_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_DATETIME_MANAGER, GsdDatetimeManagerPrivate))

#define DATETIME_SCHEMA "org.gnome.desktop.datetime"
#define AUTO_TIMEZONE_KEY "automatic-timezone"

struct GsdDatetimeManagerPrivate
{
        GSettings *settings;
        GsdTimezoneMonitor *timezone_monitor;
};

static void gsd_datetime_manager_class_init (GsdDatetimeManagerClass *klass);
static void gsd_datetime_manager_init (GsdDatetimeManager *manager);
static void gsd_datetime_manager_finalize (GObject *object);

G_DEFINE_TYPE (GsdDatetimeManager, gsd_datetime_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static void
auto_timezone_settings_changed_cb (GSettings          *settings,
                                   const char         *key,
                                   GsdDatetimeManager *self)
{
        gboolean enabled;

        enabled = g_settings_get_boolean (settings, key);
        if (enabled && self->priv->timezone_monitor == NULL) {
                g_debug ("Automatic timezone enabled");
                self->priv->timezone_monitor = gsd_timezone_monitor_new ();
        } else {
                g_debug ("Automatic timezone disabled");
                g_clear_object (&self->priv->timezone_monitor);
        }
}

gboolean
gsd_datetime_manager_start (GsdDatetimeManager *self,
                            GError            **error)
{
        g_debug ("Starting datetime manager");
        gnome_settings_profile_start (NULL);

        self->priv->settings = g_settings_new (DATETIME_SCHEMA);

        auto_timezone_settings_changed_cb (self->priv->settings, AUTO_TIMEZONE_KEY, self);
        g_signal_connect (self->priv->settings, "changed::" AUTO_TIMEZONE_KEY,
                          G_CALLBACK (auto_timezone_settings_changed_cb), self);

        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_datetime_manager_stop (GsdDatetimeManager *self)
{
        g_debug ("Stopping datetime manager");

        g_clear_object (&self->priv->settings);
        g_clear_object (&self->priv->timezone_monitor);
}

static GObject *
gsd_datetime_manager_constructor (GType type,
                                  guint n_construct_properties,
                                  GObjectConstructParam *construct_properties)
{
        GsdDatetimeManager *m;

        m = GSD_DATETIME_MANAGER (G_OBJECT_CLASS (gsd_datetime_manager_parent_class)->constructor (type,
                                                                                                   n_construct_properties,
                                                                                                   construct_properties));

        return G_OBJECT (m);
}

static void
gsd_datetime_manager_class_init (GsdDatetimeManagerClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->constructor = gsd_datetime_manager_constructor;
        object_class->finalize = gsd_datetime_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdDatetimeManagerPrivate));
}

static void
gsd_datetime_manager_init (GsdDatetimeManager *manager)
{
        manager->priv = GSD_DATETIME_MANAGER_GET_PRIVATE (manager);
}

static void
gsd_datetime_manager_finalize (GObject *object)
{
        GsdDatetimeManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_DATETIME_MANAGER (object));

        manager = GSD_DATETIME_MANAGER (object);

        g_return_if_fail (manager->priv != NULL);

        gsd_datetime_manager_stop (manager);

        G_OBJECT_CLASS (gsd_datetime_manager_parent_class)->finalize (object);
}

GsdDatetimeManager *
gsd_datetime_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_DATETIME_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_DATETIME_MANAGER (manager_object);
}
