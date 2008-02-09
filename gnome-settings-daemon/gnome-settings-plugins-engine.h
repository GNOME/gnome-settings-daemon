/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2002-2005 - Paolo Maggi
 * Copyright (C) 2007        William Jon McCann <mccann@jhu.edu>
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

#ifndef __GNOME_SETTINGS_PLUGINS_ENGINE_H__
#define __GNOME_SETTINGS_PLUGINS_ENGINE_H__

#include <glib.h>

typedef struct _GnomeSettingsPluginInfo GnomeSettingsPluginInfo;

gboolean         gnome_settings_plugins_engine_init                   (const char *gconf_prefix);
void             gnome_settings_plugins_engine_shutdown               (void);

void             gnome_settings_plugins_engine_garbage_collect        (void);

const GSList    *gnome_settings_plugins_engine_get_plugins_list       (void);

gboolean         gnome_settings_plugins_engine_activate_plugin        (GnomeSettingsPluginInfo *info);
gboolean         gnome_settings_plugins_engine_deactivate_plugin      (GnomeSettingsPluginInfo *info);
gboolean         gnome_settings_plugins_engine_plugin_is_active       (GnomeSettingsPluginInfo *info);
gboolean         gnome_settings_plugins_engine_plugin_is_available    (GnomeSettingsPluginInfo *info);

const char      *gnome_settings_plugins_engine_get_plugin_name        (GnomeSettingsPluginInfo *info);
const char      *gnome_settings_plugins_engine_get_plugin_description (GnomeSettingsPluginInfo *info);
const char     **gnome_settings_plugins_engine_get_plugin_authors     (GnomeSettingsPluginInfo *info);
const char      *gnome_settings_plugins_engine_get_plugin_website     (GnomeSettingsPluginInfo *info);
const char      *gnome_settings_plugins_engine_get_plugin_copyright   (GnomeSettingsPluginInfo *info);
gint             gnome_settings_plugins_engine_get_plugin_priority    (GnomeSettingsPluginInfo *info);

#endif  /* __GNOME_SETTINGS_PLUGINS_ENGINE_H__ */
