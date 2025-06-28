/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * vim: set et sw=8 ts=8:
 *
 * Copyright (c) 2008, Novell, Inc.
 *
 * Authors: Vincent Untz <vuntz@gnome.org>
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __GSD_DISK_SPACE_H
#define __GSD_DISK_SPACE_H

#include <gio/gio.h>
#include <glib.h>

G_BEGIN_DECLS

typedef struct {
        grefcount  ref_count;
        GFile           *file;
        GCancellable    *cancellable;
        GDateTime       *old;
        gboolean         dry_run;
        gboolean         trash;
        gchar           *name;
        gchar           *filesystem;
        gint             depth;
} DeleteData;

void delete_data_unref (DeleteData *data);
DeleteData *delete_data_new (GFile        *file,
                             GCancellable *cancellable,
                             GDateTime    *old,
                             gboolean      dry_run,
                             gboolean      trash,
                             gint          depth,
                             const char   *filesystem);
char* get_filesystem (GFile *file);

void delete_recursively_by_age (DeleteData *data);

void gsd_ldsm_setup (gboolean check_now);
void gsd_ldsm_clean (void);

/* for the test */
void gsd_ldsm_show_empty_trash (void);
void gsd_ldsm_purge_trash      (GDateTime *old);
void gsd_ldsm_purge_temp_files (GDateTime *old);

G_END_DECLS

#endif /* __GSD_DISK_SPACE_H */
