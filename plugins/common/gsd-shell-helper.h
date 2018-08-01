/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Carlos Garnacho <carlosg@gnome.org>
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

#ifndef __GSD_SHELL_HELPER_H__
#define __GSD_SHELL_HELPER_H__

#include "gsd-shell-glue.h"

G_BEGIN_DECLS

void shell_show_osd (GsdShell    *shell,
		     const gchar *icon_name,
		     const gchar *label,
		     gint         level,
		     gint         monitor);

void shell_show_osd_with_max_level (GsdShell    *shell,
                                    const gchar *icon_name,
                                    const gchar *label,
                                    gint         level,
                                    gint         max_level,
                                    gint         monitor);

G_END_DECLS

#endif /* __GSD_SHELL_HELPER_H__ */
