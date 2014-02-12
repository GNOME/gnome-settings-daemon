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

#ifndef __GSD_XKB_UTILS_H
#define __GSD_XKB_UTILS_H

#include <X11/XKBlib.h>
#include <X11/extensions/XKBrules.h>

void
gsd_xkb_get_var_defs (char             **rules,
                      XkbRF_VarDefsRec **var_defs);
void
gsd_xkb_free_var_defs (XkbRF_VarDefsRec *var_defs);

#endif  /* __GSD_XKB_UTILS_H */
