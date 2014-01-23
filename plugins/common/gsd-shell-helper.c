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

#include "config.h"
#include "gsd-shell-helper.h"

void
shell_show_osd (GsdShell    *shell,
		const gchar *icon_name,
		const gchar *label,
		gint         level,
		gint         monitor)
{
	GVariantBuilder builder;

        g_return_if_fail (GSD_IS_SHELL (shell));

        g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);

        if (icon_name)
                g_variant_builder_add (&builder, "{sv}",
                                       "icon", g_variant_new_string (icon_name));
        if (label)
                g_variant_builder_add (&builder, "{sv}",
                                       "label", g_variant_new_string (label));
        if (level >= 0)
                g_variant_builder_add (&builder, "{sv}",
                                       "level", g_variant_new_int32 (level));
        if (monitor >= 0)
                g_variant_builder_add (&builder, "{sv}",
                                       "monitor", g_variant_new_int32 (monitor));

	gsd_shell_call_show_osd (shell,
				 g_variant_builder_end (&builder),
				 NULL, NULL, NULL);
}
