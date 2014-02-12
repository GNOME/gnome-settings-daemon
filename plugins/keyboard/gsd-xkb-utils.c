/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Red Hat, Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#include "gsd-xkb-utils.h"

#ifndef XKB_RULES_FILE
#define XKB_RULES_FILE "evdev"
#endif
#ifndef XKB_LAYOUT
#define XKB_LAYOUT "us"
#endif
#ifndef XKB_MODEL
#define XKB_MODEL "pc105+inet"
#endif

void
gsd_xkb_get_var_defs (char             **rules,
                      XkbRF_VarDefsRec **var_defs)
{
        Display *display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
        char *tmp;

        g_return_if_fail (rules != NULL);
        g_return_if_fail (var_defs != NULL);

        *rules = NULL;
        *var_defs = g_new0 (XkbRF_VarDefsRec, 1);

        gdk_error_trap_push ();

        /* Get it from the X property or fallback on defaults */
        if (!XkbRF_GetNamesProp (display, rules, *var_defs) || !*rules) {
                *rules = strdup (XKB_RULES_FILE);
                (*var_defs)->model = strdup (XKB_MODEL);
                (*var_defs)->layout = strdup (XKB_LAYOUT);
                (*var_defs)->variant = NULL;
                (*var_defs)->options = NULL;
        }

        gdk_error_trap_pop_ignored ();

        tmp = *rules;

        if (*rules[0] == '/')
                *rules = g_strdup (*rules);
        else
                *rules = g_build_filename (XKB_BASE, "rules", *rules, NULL);

        free (tmp);
}

void
gsd_xkb_free_var_defs (XkbRF_VarDefsRec *var_defs)
{
        g_return_if_fail (var_defs != NULL);

        free (var_defs->model);
        free (var_defs->layout);
        free (var_defs->variant);
        free (var_defs->options);

        g_free (var_defs);
}

