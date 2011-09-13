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

static void settings_changed_cb (GSettings *settings,
                                 const gchar *key,
                                 ConfWatcher *watcher);

static void
conf_watcher_finalize (GObject *object)
{
	ConfWatcher *watcher = CONF_WATCHER (object);

	if (watcher->settings != NULL) {
                g_signal_handlers_disconnect_by_func (watcher->settings, settings_changed_cb, watcher);
		g_object_unref (watcher->settings);
        }

	if (watcher->conf_client != NULL)
		g_object_unref (watcher->conf_client);

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

static void
settings_changed_cb (GSettings *settings,
		     const gchar *key,
		     ConfWatcher *watcher)
{
	const gchar *gconf_key_name;

	gconf_key_name = g_hash_table_lookup (watcher->keys_hash, key);
	if (gconf_key_name != NULL) {
		GVariant *value;

		value = g_settings_get_value (settings, key);
		if (g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN)) {
			gconf_client_set_bool (watcher->conf_client,
					       gconf_key_name,
					       g_variant_get_boolean (value),
					       NULL);
		} else if (g_variant_is_of_type (value, G_VARIANT_TYPE_BYTE) ||
			   g_variant_is_of_type (value, G_VARIANT_TYPE_INT16) ||
			   g_variant_is_of_type (value, G_VARIANT_TYPE_UINT16) ||
			   g_variant_is_of_type (value, G_VARIANT_TYPE_INT32) ||
			   g_variant_is_of_type (value, G_VARIANT_TYPE_UINT32) ||
			   g_variant_is_of_type (value, G_VARIANT_TYPE_INT64) ||
			   g_variant_is_of_type (value, G_VARIANT_TYPE_UINT64)) {
			gconf_client_set_int (watcher->conf_client,
					      gconf_key_name,
					      g_settings_get_int (settings, key),
					      NULL);
		} else if (g_variant_is_of_type (value, G_VARIANT_TYPE_STRING)) {
			gconf_client_set_string (watcher->conf_client,
						 gconf_key_name,
						 g_variant_get_string (value, NULL),
						 NULL);
		} else if (g_variant_is_of_type (value, G_VARIANT_TYPE_DOUBLE)) {
			gconf_client_set_float (watcher->conf_client,
						gconf_key_name,
						g_variant_get_double (value),
						NULL);
		} else if (g_variant_is_of_type (value, G_VARIANT_TYPE_STRING_ARRAY)) {
			const gchar **items;
			gsize len, i;
			GSList *gconf_list = NULL;

			items = g_variant_get_strv (value, &len);
			for (i = 0; i < len; i++) {
				gconf_list = g_slist_append (gconf_list, (gpointer) items[i]);
			}

			gconf_client_set_list (watcher->conf_client,
					       gconf_key_name,
					       GCONF_VALUE_STRING,
					       gconf_list,
					       NULL);

			g_slist_free (gconf_list);
			g_free (items);
		}
	}
}

static void
setup_watcher (ConfWatcher *watcher)
{
	watcher->settings = g_settings_new (watcher->settings_id);
	g_signal_connect (watcher->settings, "changed",
			  G_CALLBACK (settings_changed_cb), watcher);

	watcher->conf_client = gconf_client_get_default ();
}

ConfWatcher *
conf_watcher_new (const gchar *settings_id, GHashTable *keys_hash)
{
	ConfWatcher *watcher;

	g_return_val_if_fail (settings_id != NULL, NULL);
	g_return_val_if_fail (keys_hash != NULL, NULL);

	watcher = g_object_new (TYPE_CONF_WATCHER, NULL);

	watcher->settings_id = g_strdup (settings_id);
	watcher->keys_hash = keys_hash;
	setup_watcher (watcher);

	return watcher;
}
