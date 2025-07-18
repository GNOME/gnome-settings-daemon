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
        GMainLoop *loop;

        notify_init ("gsd-disk-space-test");

        loop = g_main_loop_new (NULL, FALSE);

        gsd_ldsm_setup (TRUE);

        g_main_loop_run (loop);

        gsd_ldsm_clean ();
        g_main_loop_unref (loop);

        return 0;
}

