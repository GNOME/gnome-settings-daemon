/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * vim: set et sw=8 ts=8:
 *
 * Copyright (c) 2011, Red Hat, Inc.
 *
 * Authors: Cosimo Cecchi <cosimoc@redhat.com>
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
#include "gsd-disk-space.h"

int
main (int    argc,
      char **argv)
{
        GMainLoop *loop;

        loop = g_main_loop_new (NULL, FALSE);

        gsd_ldsm_show_empty_trash ();
        g_main_loop_run (loop);

        g_main_loop_unref (loop);

        return 0;
}

