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
#include <gconf/gconf-client.h>

#include "gnome-settings-plugin-info.h"
#include "gnome-settings-manager.h"
#include "gnome-settings-manager-glue.h"
#include "gnome-settings-profile.h"

#define GSD_MANAGER_DBUS_PATH "/org/gnome/SettingsDaemon"

#define DEFAULT_SETTINGS_PREFIX "/apps/gnome_settings_daemon/plugins"

#define PLUGIN_EXT ".gnome-settings-plugin"

#define GNOME_SETTINGS_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GNOME_TYPE_SETTINGS_MANAGER, GnomeSettingsManagerPrivate))

struct GnomeSettingsManagerPrivate
{
        DBusGConnection            *connection;
        GConfClient                *gconf_client;
        char                       *settings_prefix;
        GSList                     *plugins;
};

enum {
        PROP_0,
        PROP_SETTINGS_PREFIX,
};

enum {
        PLUGIN_ACTIVATED,
        PLUGIN_DEACTIVATED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gnome_settings_manager_class_init  (GnomeSettingsManagerClass *klass);
static void     gnome_settings_manager_init        (GnomeSettingsManager      *settings_manager);
static void     gnome_settings_manager_finalize    (GObject                   *object);

G_DEFINE_TYPE (GnomeSettingsManager, gnome_settings_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

GQuark
gnome_settings_manager_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("gnome_settings_manager_error");
        }

        return ret;
}

static void
maybe_activate_plugin (GnomeSettingsPluginInfo *info, gpointer user_data)
{
        if (gnome_settings_plugin_info_get_enabled (info)) {
                gboolean res;
                res = gnome_settings_plugin_info_activate (info);
                if (res) {
                        g_debug ("Plugin %s: active", gnome_settings_plugin_info_get_location (info));
                } else {
                        g_debug ("Plugin %s: activation failed", gnome_settings_plugin_info_get_location (info));
                }
        } else {
                g_debug ("Plugin %s: inactive", gnome_settings_plugin_info_get_location (info));
        }
}

static gint
compare_location (GnomeSettingsPluginInfo *a,
                  GnomeSettingsPluginInfo *b)
{
        const char *loc_a;
        const char *loc_b;

        loc_a = gnome_settings_plugin_info_get_location (a);
        loc_b = gnome_settings_plugin_info_get_location (b);

        if (loc_a == NULL || loc_b == NULL) {
                return -1;
        }

        return strcmp (loc_a, loc_b);
}

static int
compare_priority (GnomeSettingsPluginInfo *a,
                  GnomeSettingsPluginInfo *b)
{
        int prio_a;
        int prio_b;

        prio_a = gnome_settings_plugin_info_get_priority (a);
        prio_b = gnome_settings_plugin_info_get_priority (b);

        return prio_a - prio_b;
}

static void
on_plugin_activated (GnomeSettingsPluginInfo *info,
                     GnomeSettingsManager    *manager)
{
        const char *name;
        name = gnome_settings_plugin_info_get_location (info);
        g_debug ("GnomeSettingsManager: emitting plugin-activated %s", name);
        g_signal_emit (manager, signals [PLUGIN_ACTIVATED], 0, name);
}

static void
on_plugin_deactivated (GnomeSettingsPluginInfo *info,
                       GnomeSettingsManager    *manager)
{
        const char *name;
        name = gnome_settings_plugin_info_get_location (info);
        g_debug ("GnomeSettingsManager: emitting plugin-deactivated %s", name);
        g_signal_emit (manager, signals [PLUGIN_DEACTIVATED], 0, name);
}

static void
_load_file (GnomeSettingsManager *manager,
            const char           *filename)
{
        GnomeSettingsPluginInfo *info;
        char                    *key_name;
        int                      priority;
        GError                  *error;
        GSList                  *l;

        g_debug ("Loading plugin: %s", filename);
        gnome_settings_profile_start ("%s", filename);

        info = gnome_settings_plugin_info_new_from_file (filename);
        if (info == NULL) {
                goto out;
        }

        l = g_slist_find_custom (manager->priv->plugins,
                                 info,
                                 (GCompareFunc) compare_location);
        if (l != NULL) {
                goto out;
        }

        manager->priv->plugins = g_slist_prepend (manager->priv->plugins,
                                                  g_object_ref (info));

        g_signal_connect (info, "activated",
                          G_CALLBACK (on_plugin_activated), manager);
        g_signal_connect (info, "deactivated",
                          G_CALLBACK (on_plugin_deactivated), manager);

        key_name = g_strdup_printf ("%s/%s/active",
                                    manager->priv->settings_prefix,
                                    gnome_settings_plugin_info_get_location (info));
        gnome_settings_plugin_info_set_enabled_key_name (info, key_name);
        g_free (key_name);

        key_name = g_strdup_printf ("%s/%s/priority",
                                    manager->priv->settings_prefix,
                                    gnome_settings_plugin_info_get_location (info));
        error = NULL;
        priority = gconf_client_get_int (manager->priv->gconf_client, key_name, &error);
        if (error == NULL) {
                if (priority > 0) {
                        gnome_settings_plugin_info_set_priority (info, priority);
                }
        } else {
                g_error_free (error);
        }
        g_free (key_name);

 out:
        if (info != NULL) {
                g_object_unref (info);
        }

        gnome_settings_profile_end ("%s", filename);
}

static void
_load_dir (GnomeSettingsManager *manager,
           const char           *path)
{
        GError     *error;
        GDir       *d;
        const char *name;

        g_debug ("Loading settings plugins from dir: %s", path);
        gnome_settings_profile_start (NULL);

        error = NULL;
        d = g_dir_open (path, 0, &error);
        if (d == NULL) {
                g_warning ("%s", error->message);
                g_error_free (error);
                return;
        }

        while ((name = g_dir_read_name (d))) {
                char *filename;

                if (!g_str_has_suffix (name, PLUGIN_EXT)) {
                        continue;
                }

                filename = g_build_filename (path, name, NULL);
                if (g_file_test (filename, G_FILE_TEST_IS_REGULAR)) {
                        _load_file (manager, filename);
                }
                g_free (filename);
        }

        g_dir_close (d);

        gnome_settings_profile_end (NULL);
}

static void
_load_all (GnomeSettingsManager *manager)
{
        gnome_settings_profile_start (NULL);

        /* load system plugins */
        _load_dir (manager, GNOME_SETTINGS_PLUGINDIR G_DIR_SEPARATOR_S);

        manager->priv->plugins = g_slist_sort (manager->priv->plugins, (GCompareFunc) compare_priority);
        g_slist_foreach (manager->priv->plugins, (GFunc) maybe_activate_plugin, NULL);
        gnome_settings_profile_end (NULL);
}

static void
_unload_plugin (GnomeSettingsPluginInfo *info, gpointer user_data)
{
        if (gnome_settings_plugin_info_get_enabled (info)) {
                gnome_settings_plugin_info_deactivate (info);
        }
        g_object_unref (info);
}

static void
_unload_all (GnomeSettingsManager *manager)
{
         g_slist_foreach (manager->priv->plugins, (GFunc) _unload_plugin, NULL);
         g_slist_free (manager->priv->plugins);
         manager->priv->plugins = NULL;
}

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
        return gnome_settings_manager_start (manager, error);
}

static gboolean
register_manager (GnomeSettingsManager *manager)
{
        GError *error = NULL;

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

        ret = FALSE;

        gnome_settings_profile_start (NULL);

        if (!g_module_supported ()) {
                g_warning ("gnome-settings-daemon is not able to initialize the plugins.");
                g_set_error (error,
                             GNOME_SETTINGS_MANAGER_ERROR,
                             GNOME_SETTINGS_MANAGER_ERROR_GENERAL,
                             "Plugins not supported");

                goto out;
        }

        manager->priv->gconf_client = gconf_client_get_default ();

        gnome_settings_profile_start ("preloading gconf keys");
        gconf_client_add_dir (manager->priv->gconf_client,
                              manager->priv->settings_prefix,
                              GCONF_CLIENT_PRELOAD_RECURSIVE,
                              NULL);
        gnome_settings_profile_end ("preloading gconf keys");

        _load_all (manager);

        ret = TRUE;
 out:
        gnome_settings_profile_end (NULL);

        return ret;
}

gboolean
gnome_settings_manager_start_with_settings_prefix (GnomeSettingsManager *manager,
                                                   const char           *settings_prefix,
                                                   GError              **error)
{
        g_object_set (manager, "settings-prefix", settings_prefix, NULL);

        return gnome_settings_manager_start (manager, error);
}

void
gnome_settings_manager_stop (GnomeSettingsManager *manager)
{
        g_debug ("Stopping settings manager");

#ifdef ENABLE_PYTHON
        /* Note: that this may cause finalization of objects by
         * running the garbage collector. Since some of the plugin may
         * have installed callbacks upon object finalization it must
         * run before we get rid of the plugins.
         */
        gnome_settings_python_shutdown ();
#endif

        _unload_all (manager);

        gconf_client_remove_dir (manager->priv->gconf_client,
                                 manager->priv->settings_prefix,
                                 NULL);
        g_object_unref (manager->priv->gconf_client);
        manager->priv->gconf_client = NULL;
}

static void
_set_settings_prefix (GnomeSettingsManager *self,
                   const char           *prefix)
{
        g_free (self->priv->settings_prefix);
        self->priv->settings_prefix = g_strdup (prefix);
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
        case PROP_SETTINGS_PREFIX:
                _set_settings_prefix (self, g_value_get_string (value));
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
        case PROP_SETTINGS_PREFIX:
                g_value_set_string (value, self->priv->settings_prefix);
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

        signals [PLUGIN_ACTIVATED] =
                g_signal_new ("plugin-activated",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GnomeSettingsManagerClass, plugin_activated),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [PLUGIN_DEACTIVATED] =
                g_signal_new ("plugin-deactivated",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GnomeSettingsManagerClass, plugin_deactivated),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);

        g_object_class_install_property (object_class,
                                         PROP_SETTINGS_PREFIX,
                                         g_param_spec_string ("settings-prefix",
                                                              "settings-prefix",
                                                              "settings-prefix",
                                                              DEFAULT_SETTINGS_PREFIX,
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

        g_free (manager->priv->settings_prefix);

        G_OBJECT_CLASS (gnome_settings_manager_parent_class)->finalize (object);
}

GnomeSettingsManager *
gnome_settings_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                gboolean res;

                manager_object = g_object_new (GNOME_TYPE_SETTINGS_MANAGER,
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
