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

#include "config.h"
#include <libnotify/notify.h>
#include "gsd-disk-space.h"

int
main (int    argc,
      char **argv)
{
        GFile *file;
        DeleteData *data;
        GDateTime *old;
        GMainLoop *loop;
        g_autofree char *filesystem = NULL;

        g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

        notify_init ("gsd-purge-temp-test");
        loop = g_main_loop_new (NULL, FALSE);

        file = g_file_new_for_path ("/tmp/gsd-purge-temp-test");
        if (!g_file_query_exists (file, NULL)) {
                g_warning ("Create /tmp/gsd-purge-temp-test and add some files to it to test deletion by date");
                g_object_unref (file);
                return 1;
        }

        old = g_date_time_new_now_local ();
        filesystem = get_filesystem (file);
        data = delete_data_new (file, NULL, old, FALSE, FALSE, 0, filesystem);
        delete_recursively_by_age (data);
        delete_data_unref (data);
        g_object_unref (file);

        g_main_loop_run (loop);

        return 0;
}

