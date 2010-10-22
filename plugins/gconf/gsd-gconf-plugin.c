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
#include <gmodule.h>
#include "gnome-settings-plugin.h"
#include "gsd-gconf-plugin.h"
#include "gsd-gconf-manager.h"

struct GsdGconfPluginPrivate {
	GsdGconfManager *manager;
};

#define GSD_GCONF_PLUGIN_GET_PRIVATE(object) (G_TYPE_INSTANCE_GET_PRIVATE ((object), GSD_TYPE_GCONF_PLUGIN, GsdGconfPluginPrivate))

GNOME_SETTINGS_PLUGIN_REGISTER(GsdGconfPlugin, gsd_gconf_plugin)

static void
gsd_gconf_plugin_init (GsdGconfPlugin *plugin)
{
	plugin->priv = GSD_GCONF_PLUGIN_GET_PRIVATE (plugin);
	plugin->priv->manager = gsd_gconf_manager_new ();
}

static void
gsd_gconf_plugin_finalize (GObject *object)
{
	GsdGconfPlugin *plugin;

	plugin = GSD_GCONF_PLUGIN (object);

	g_return_if_fail (plugin->priv != NULL);

	if (plugin->priv->manager != NULL)
		g_object_unref (plugin->priv->manager);

	G_OBJECT_CLASS (gsd_gconf_plugin_parent_class)->finalize (object);
}

static void
impl_activate (GnomeSettingsPlugin *plugin)
{
	gboolean res;
	GError *error = NULL;

	g_debug ("Starting GConf bridge plugin");
	res = gsd_gconf_manager_start (GSD_GCONF_PLUGIN (plugin)->priv->manager, &error);
	if (!res) {
		g_warning ("Unable to start GConf manager: %s", error->message);
		g_error_free (error);
	}
}

static void
impl_deactivate (GnomeSettingsPlugin *plugin)
{
	g_debug ("Stopping GConf bridge plugin");
	gsd_gconf_manager_stop (GSD_GCONF_PLUGIN (plugin)->priv->manager);
}

static void
gsd_gconf_plugin_class_init (GsdGconfPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GnomeSettingsPluginClass *plugin_class = GNOME_SETTINGS_PLUGIN_CLASS (klass);

	object_class->finalize = gsd_gconf_plugin_finalize;

	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;

	g_type_class_add_private (klass, sizeof (GsdGconfPluginPrivate));
}

static void
gsd_gconf_plugin_class_finalize (GsdGconfPluginClass *klass)
{
}
