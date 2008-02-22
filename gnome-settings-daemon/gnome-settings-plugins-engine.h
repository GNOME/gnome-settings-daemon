/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2002-2005 Paolo Maggi
 * Copyright (C) 2007      William Jon McCann <mccann@jhu.edu>
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

#ifndef __GNOME_SETTINGS_PLUGINS_ENGINE_H__
#define __GNOME_SETTINGS_PLUGINS_ENGINE_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define GNOME_TYPE_SETTINGS_PLUGINS_ENGINE         (gnome_settings_plugins_engine_get_type ())
#define GNOME_SETTINGS_PLUGINS_ENGINE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNOME_TYPE_SETTINGS_PLUGINS_ENGINE, GnomeSettingsPluginsEngine))
#define GNOME_SETTINGS_PLUGINS_ENGINE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GNOME_TYPE_SETTINGS_PLUGINS_ENGINE, GnomeSettingsPluginsEngineClass))
#define GNOME_IS_SETTINGS_PLUGINS_ENGINE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNOME_TYPE_SETTINGS_PLUGINS_ENGINE))
#define GNOME_IS_SETTINGS_PLUGINS_ENGINE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GNOME_TYPE_SETTINGS_PLUGINS_ENGINE))
#define GNOME_SETTINGS_PLUGINS_ENGINE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GNOME_TYPE_SETTINGS_PLUGINS_ENGINE, GnomeSettingsPluginsEngineClass))

typedef struct GnomeSettingsPluginsEnginePrivate GnomeSettingsPluginsEnginePrivate;

typedef struct
{
        GObject                            parent;
        GnomeSettingsPluginsEnginePrivate *priv;
} GnomeSettingsPluginsEngine;

typedef struct
{
        GObjectClass   parent_class;
} GnomeSettingsPluginsEngineClass;


GType                       gnome_settings_plugins_engine_get_type               (void);

GnomeSettingsPluginsEngine *gnome_settings_plugins_engine_new                    (const char *gconf_prefix);
gboolean         gnome_settings_plugins_engine_start                  (GnomeSettingsPluginsEngine *engine);

gboolean         gnome_settings_plugins_engine_stop                   (GnomeSettingsPluginsEngine *engine);

void             gnome_settings_plugins_engine_garbage_collect        (GnomeSettingsPluginsEngine *engine);

const GSList    *gnome_settings_plugins_engine_get_plugins_list       (GnomeSettingsPluginsEngine *engine);

G_END_DECLS

#endif  /* __GNOME_SETTINGS_PLUGINS_ENGINE_H__ */
