/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2025 Red Hat Inc.
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#ifndef __GSD_APPLICATION_H__
#define __GSD_APPLICATION_H__

#include <gio/gio.h>
#include <canberra.h>

#define GSD_TYPE_APPLICATION gsd_application_get_type ()
G_DECLARE_DERIVABLE_TYPE (GsdApplication,
			  gsd_application,
			  GSD, APPLICATION,
			  GApplication)

struct _GsdApplicationClass
{
	GApplicationClass parent_class;

	void (* pre_shutdown) (GsdApplication *app);
};

void gsd_application_pre_shutdown (GsdApplication *app);

ca_context * gsd_application_get_ca_context (GsdApplication *app);

#endif /* __GSD_APPLICATION_H__ */
