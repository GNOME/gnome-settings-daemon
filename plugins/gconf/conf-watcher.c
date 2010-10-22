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

#include "config.h"
#include "conf-watcher.h"

G_DEFINE_TYPE(ConfWatcher, conf_watcher, G_TYPE_OBJECT)

static void
conf_watcher_finalize (GObject *object)
{
	ConfWatcher *watcher = CONF_WATCHER (object);

	if (watcher->settings_id != NULL)
		g_free (watcher->settings_id);

	if (watcher->keys_hash != NULL)
		g_hash_table_destroy (watcher->keys_hash);

	G_OBJECT_CLASS (conf_watcher_parent_class)->finalize (object);
}

static void
conf_watcher_class_init (ConfWatcherClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = conf_watcher_finalize;
}

static void
conf_watcher_init (ConfWatcher *watcher)
{
}

ConfWatcher *
conf_watcher_new (const gchar *settings_id, GHashTable *keys_hash)
{
	ConfWatcher *watcher;

	g_return_val_if_fail (settings_id != NULL, NULL);
	g_return_val_if_fail (keys_hash != NULL, NULL);

	watcher = g_object_new (TYPE_CONF_WATCHER, NULL);

	return watcher;
}
