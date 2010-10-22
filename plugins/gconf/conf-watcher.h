/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Rodrigo Moya <rodrigo@gnome.org>
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

#ifndef __CONF_WATCHER_H
#define __CONF_WATCHER_H

#include <glib-object.h>
#include <gio/gio.h>
#include <gconf/gconf-client.h>

G_BEGIN_DECLS

#define TYPE_CONF_WATCHER         (conf_watcher_get_type ())
#define CONF_WATCHER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TYPE_CONF_WATCHER, ConfWatcher))
#define CONF_WATCHER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), TYPE_CONF_WATCHER, ConfWatcherClass))
#define IS_CONF_WATCHER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TYPE_CONF_WATCHER))
#define IS_CONF_WATCHER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), TYPE_CONF_WATCHER))
#define CONF_WATCHER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), TYPE_CONF_WATCHER, ConfWatcherClass))

typedef struct ConfWatcherPrivate ConfWatcherPrivate;

typedef struct
{
        GObject parent;

	/* Private data */
	GSettings *settings;
	GConfClient *conf_client;
	gchar *settings_id;
	GHashTable *keys_hash;
} ConfWatcher;

typedef struct
{
        GObjectClass   parent_class;
} ConfWatcherClass;

GType        conf_watcher_get_type (void);

ConfWatcher *conf_watcher_new      (const gchar *settings_id, GHashTable *keys_hash);

G_END_DECLS

#endif /* __CONF_WATCHER_H */
