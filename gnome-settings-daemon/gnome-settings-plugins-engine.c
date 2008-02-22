/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2002-2005 Paolo Maggi
 * Copyright (C) 2007      William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2007      Jens Granseuer <jensgr@gmx.net>
 * Copyright (C) 2008      Red Hat, Inc.
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
 * Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>

#include "gnome-settings-plugins-engine.h"
#include "gnome-settings-plugin-info.h"

#define PLUGIN_EXT ".gnome-settings-plugin"

#define GNOME_SETTINGS_PLUGINS_ENGINE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GNOME_TYPE_SETTINGS_PLUGINS_ENGINE, GnomeSettingsPluginsEnginePrivate))

struct GnomeSettingsPluginsEnginePrivate
{
        char        *gconf_prefix;
        GSList      *plugins;
};

enum {
        PROP_0,
        PROP_GCONF_PREFIX,
};

static void     gnome_settings_plugins_engine_class_init  (GnomeSettingsPluginsEngineClass *klass);
static void     gnome_settings_plugins_engine_init        (GnomeSettingsPluginsEngine      *settings_plugins_engine);
static void     gnome_settings_plugins_engine_finalize    (GObject                   *object);

G_DEFINE_TYPE (GnomeSettingsPluginsEngine, gnome_settings_plugins_engine, G_TYPE_OBJECT)

static gpointer engine_object = NULL;

static GnomeSettingsPluginInfo *
gnome_settings_plugins_engine_load (GnomeSettingsPluginsEngine *engine,
                                    const char                 *file)
{
        GnomeSettingsPluginInfo *info;

        g_return_val_if_fail (file != NULL, NULL);

        g_debug ("Loading plugin: %s", file);

        info = gnome_settings_plugin_info_new_from_file (file);

        return info;
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
gnome_settings_plugins_engine_load_file (GnomeSettingsPluginsEngine *engine,
                                         const char                 *filename)
{
        GnomeSettingsPluginInfo *info;
        char                    *key_name;

        info = gnome_settings_plugins_engine_load (engine, filename);
        if (info == NULL) {
                return;
        }

        if (g_slist_find_custom (engine->priv->plugins,
                                 info,
                                 (GCompareFunc) compare_location)) {
                g_object_unref (info);
                return;
        }

        engine->priv->plugins = g_slist_prepend (engine->priv->plugins, info);


        key_name = g_strdup_printf ("%s/%s/active",
                                    engine->priv->gconf_prefix,
                                    gnome_settings_plugin_info_get_location (info));
        gnome_settings_plugin_info_set_enabled_key_name (info, key_name);
        g_free (key_name);
}

static void
gnome_settings_plugins_engine_load_dir (GnomeSettingsPluginsEngine *engine,
                                        const char                 *path)
{
        GError     *error;
        GDir       *d;
        const char *name;

        g_debug ("Loading settings plugins from dir: %s", path);

        error = NULL;
        d = g_dir_open (path, 0, &error);
        if (d == NULL) {
                g_warning (error->message);
                g_error_free (error);
                return;
        }

        while ((name = g_dir_read_name (d))) {
                char *filename;

                if (!g_str_has_suffix (name, PLUGIN_EXT))
                        continue;

                filename = g_build_filename (path, name, NULL);
                if (g_file_test (filename, G_FILE_TEST_IS_REGULAR)) {
                        gnome_settings_plugins_engine_load_file (engine, filename);
                }
                g_free (filename);
        }

        g_dir_close (d);
}

static void
gnome_settings_plugins_engine_load_all (GnomeSettingsPluginsEngine *engine)
{
        /* load system plugins */
        gnome_settings_plugins_engine_load_dir (engine,
                                                GNOME_SETTINGS_PLUGINDIR G_DIR_SEPARATOR_S);

        engine->priv->plugins = g_slist_sort (engine->priv->plugins, (GCompareFunc) compare_priority);
        g_slist_foreach (engine->priv->plugins, (GFunc) maybe_activate_plugin, NULL);
}

static void
gnome_settings_plugins_engine_unload_all (GnomeSettingsPluginsEngine *engine)
{
         g_slist_foreach (engine->priv->plugins, (GFunc) g_object_unref, NULL);
         g_slist_free (engine->priv->plugins);
         engine->priv->plugins = NULL;
}

gboolean
gnome_settings_plugins_engine_start (GnomeSettingsPluginsEngine *engine)
{
        if (!g_module_supported ()) {
                g_warning ("gnome-settings-daemon is not able to initialize the plugins engine.");
                return FALSE;
        }

        gnome_settings_plugins_engine_load_all (engine);

        return TRUE;
}

void
gnome_settings_plugins_engine_garbage_collect (GnomeSettingsPluginsEngine *engine)
{
#ifdef ENABLE_PYTHON
        gnome_settings_python_garbage_collect ();
#endif
}

gboolean
gnome_settings_plugins_engine_stop (GnomeSettingsPluginsEngine *engine)
{

#ifdef ENABLE_PYTHON
        /* Note: that this may cause finalization of objects by
         * running the garbage collector. Since some of the plugin may
         * have installed callbacks upon object finalization it must
         * run before we get rid of the plugins.
         */
        gnome_settings_python_shutdown ();
#endif

        gnome_settings_plugins_engine_unload_all (engine);

        return TRUE;
}

const GSList *
gnome_settings_plugins_engine_get_plugins_list (GnomeSettingsPluginsEngine *engine)
{
        return engine->priv->plugins;
}

static void
_set_gconf_prefix (GnomeSettingsPluginsEngine *self,
                   const char           *prefix)
{
        g_free (self->priv->gconf_prefix);
        self->priv->gconf_prefix = g_strdup (prefix);
}

static void
gnome_settings_plugins_engine_set_property (GObject        *object,
                                            guint           prop_id,
                                            const GValue   *value,
                                            GParamSpec     *pspec)
{
        GnomeSettingsPluginsEngine *self;

        self = GNOME_SETTINGS_PLUGINS_ENGINE (object);

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
gnome_settings_plugins_engine_get_property (GObject        *object,
                                            guint           prop_id,
                                            GValue         *value,
                                            GParamSpec     *pspec)
{
        GnomeSettingsPluginsEngine *self;

        self = GNOME_SETTINGS_PLUGINS_ENGINE (object);

        switch (prop_id) {
        case PROP_GCONF_PREFIX:
                g_value_set_string (value, self->priv->gconf_prefix);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gnome_settings_plugins_engine_class_init (GnomeSettingsPluginsEngineClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gnome_settings_plugins_engine_get_property;
        object_class->set_property = gnome_settings_plugins_engine_set_property;
        object_class->finalize = gnome_settings_plugins_engine_finalize;

        g_object_class_install_property (object_class,
                                         PROP_GCONF_PREFIX,
                                         g_param_spec_string ("gconf-prefix",
                                                              "gconf-prefix",
                                                              "gconf-prefix",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (GnomeSettingsPluginsEnginePrivate));
}

static void
gnome_settings_plugins_engine_init (GnomeSettingsPluginsEngine *engine)
{
        engine->priv = GNOME_SETTINGS_PLUGINS_ENGINE_GET_PRIVATE (engine);
}

static void
gnome_settings_plugins_engine_finalize (GObject *object)
{
        GnomeSettingsPluginsEngine *engine;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GNOME_IS_SETTINGS_PLUGINS_ENGINE (object));

        engine = GNOME_SETTINGS_PLUGINS_ENGINE (object);

        g_return_if_fail (engine->priv != NULL);

        g_free (engine->priv->gconf_prefix);

        G_OBJECT_CLASS (gnome_settings_plugins_engine_parent_class)->finalize (object);
}

GnomeSettingsPluginsEngine *
gnome_settings_plugins_engine_new (const char *gconf_prefix)
{
        if (engine_object != NULL) {
                g_object_ref (engine_object);
        } else {
                engine_object = g_object_new (GNOME_TYPE_SETTINGS_PLUGINS_ENGINE,
                                              "gconf-prefix", gconf_prefix,
                                              NULL);
                g_object_add_weak_pointer (engine_object,
                                           (gpointer *) &engine_object);
        }

        return GNOME_SETTINGS_PLUGINS_ENGINE (engine_object);
}

