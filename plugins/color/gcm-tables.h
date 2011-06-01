/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2010 Richard Hughes <richard@hughsie.com>
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

#ifndef __GCM_TABLES_H
#define __GCM_TABLES_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GCM_TYPE_TABLES                 (gcm_tables_get_type ())
#define GCM_TABLES(o)                   (G_TYPE_CHECK_INSTANCE_CAST ((o), GCM_TYPE_TABLES, GcmTables))
#define GCM_TABLES_CLASS(k)             (G_TYPE_CHECK_CLASS_CAST((k), GCM_TYPE_TABLES, GcmTablesClass))
#define GCM_IS_TABLES(o)                (G_TYPE_CHECK_INSTANCE_TYPE ((o), GCM_TYPE_TABLES))
#define GCM_IS_TABLES_CLASS(k)          (G_TYPE_CHECK_CLASS_TYPE ((k), GCM_TYPE_TABLES))
#define GCM_TABLES_GET_CLASS(o)         (G_TYPE_INSTANCE_GET_CLASS ((o), GCM_TYPE_TABLES, GcmTablesClass))
#define GCM_TABLES_ERROR                (gcm_tables_error_quark ())

typedef struct _GcmTablesPrivate        GcmTablesPrivate;
typedef struct _GcmTables               GcmTables;
typedef struct _GcmTablesClass          GcmTablesClass;

struct _GcmTables
{
         GObject                         parent;
         GcmTablesPrivate               *priv;
};

struct _GcmTablesClass
{
        GObjectClass    parent_class;
};

enum
{
        GCM_TABLES_ERROR_FAILED
};

GType            gcm_tables_get_type                    (void);
GQuark           gcm_tables_error_quark                 (void);
GcmTables       *gcm_tables_new                         (void);
gchar           *gcm_tables_get_pnp_id                  (GcmTables              *tables,
                                                         const gchar            *pnp_id,
                                                         GError                 **error);

G_END_DECLS

#endif /* __GCM_TABLES_H */

