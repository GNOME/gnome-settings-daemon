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

#include "gsd-application.h"

G_DEFINE_TYPE (GsdApplication, gsd_application, G_TYPE_APPLICATION)

static void
gsd_application_real_pre_shutdown (GsdApplication *app)
{
	g_application_release (G_APPLICATION (app));
}

static void
gsd_application_class_init (GsdApplicationClass *klass)
{
	klass->pre_shutdown = gsd_application_real_pre_shutdown;
}

static void
gsd_application_init (GsdApplication *app)
{
}

void
gsd_application_pre_shutdown (GsdApplication *app)
{
	GSD_APPLICATION_GET_CLASS (app)->pre_shutdown (app);
}
