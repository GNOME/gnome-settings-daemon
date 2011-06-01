/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2011 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib-object.h>

#include "gcm-tables.h"

static void     gcm_tables_finalize     (GObject     *object);

#define GCM_TABLES_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GCM_TYPE_TABLES, GcmTablesPrivate))

struct _GcmTablesPrivate
{
        gchar                           *data_dir;
        gchar                           *table_data;
        GHashTable                      *pnp_table;
};

static gpointer gcm_tables_object = NULL;

G_DEFINE_TYPE (GcmTables, gcm_tables, G_TYPE_OBJECT)

GQuark
gcm_tables_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("gcm_tables_error");
	return quark;
}

static gboolean
gcm_tables_load (GcmTables *tables, GError **error)
{
        gboolean ret;
        gchar *filename = NULL;
        gchar *retval = NULL;
        GcmTablesPrivate *priv = tables->priv;
        guint i;

        /* load the contents */
        g_debug ("loading: %s", PNPIDS_FILE);
        ret = g_file_get_contents (PNPIDS_FILE, &priv->table_data, NULL, error);
        if (!ret)
                goto out;

        /* parse into lines */
        retval = priv->table_data;
        for (i = 0; priv->table_data[i] != '\0'; i++) {

                /* ignore */
                if (priv->table_data[i] != '\n')
                        continue;

                /* convert newline to NULL */
                priv->table_data[i] = '\0';

                /* the ID to text is a fixed offset */
                retval[3] = '\0';
                g_hash_table_insert (priv->pnp_table,
                                     retval,
                                     retval+4);
                retval = &priv->table_data[i+1];
        }
out:
        g_free (filename);
        return ret;
}

gchar *
gcm_tables_get_pnp_id (GcmTables *tables, const gchar *pnp_id, GError **error)
{
        gboolean ret;
        gchar *retval = NULL;
        GcmTablesPrivate *priv = tables->priv;
        gpointer found;
        guint size;

        g_return_val_if_fail (GCM_IS_TABLES (tables), NULL);
        g_return_val_if_fail (pnp_id != NULL, NULL);

        /* if table is empty, try to load it */
        size = g_hash_table_size (priv->pnp_table);
        if (size == 0) {
                ret = gcm_tables_load (tables, error);
                if (!ret)
                        goto out;
        }

        /* look this up in the table */
        found = g_hash_table_lookup (priv->pnp_table, pnp_id);
        if (found == NULL) {
                g_set_error (error,
                             GCM_TABLES_ERROR,
                             GCM_TABLES_ERROR_FAILED,
                             "could not find %s", pnp_id);
                goto out;
        }

        /* return a copy */
        retval = g_strdup (found);
out:
        return retval;
}

static void
gcm_tables_class_init (GcmTablesClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        object_class->finalize = gcm_tables_finalize;
        g_type_class_add_private (klass, sizeof (GcmTablesPrivate));
}

static void
gcm_tables_init (GcmTables *tables)
{
        tables->priv = GCM_TABLES_GET_PRIVATE (tables);

        /* we don't keep malloc'd data in the hash; instead we read it
         * out into priv->table_data and then link to it in the hash */
        tables->priv->pnp_table = g_hash_table_new_full (g_str_hash,
                                                         g_str_equal,
                                                         NULL,
                                                         NULL);
}

static void
gcm_tables_finalize (GObject *object)
{
        GcmTables *tables = GCM_TABLES (object);
        GcmTablesPrivate *priv = tables->priv;

        g_free (priv->data_dir);
        g_free (priv->table_data);
        g_hash_table_unref (priv->pnp_table);

        G_OBJECT_CLASS (gcm_tables_parent_class)->finalize (object);
}

GcmTables *
gcm_tables_new (void)
{
        if (gcm_tables_object != NULL) {
                g_object_ref (gcm_tables_object);
        } else {
                gcm_tables_object = g_object_new (GCM_TYPE_TABLES, NULL);
                g_object_add_weak_pointer (gcm_tables_object, &gcm_tables_object);
        }
        return GCM_TABLES (gcm_tables_object);
}

