/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010-2011 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdlib.h>

#include "config.h"

#include "gsd-backlight-linux.h"

#include <stdlib.h>

#if HAVE_GUDEV
#include <gudev/gudev.h>

static gchar *
gsd_backlight_helper_get_type (GList *devices, const gchar *type)
{
	const gchar *type_tmp;
	GList *d;

	for (d = devices; d != NULL; d = d->next) {
		type_tmp = g_udev_device_get_sysfs_attr (d->data, "type");
		if (g_strcmp0 (type_tmp, type) == 0)
			return g_strdup (g_udev_device_get_sysfs_path (d->data));
	}
	return NULL;
}

/*
 * Search for a raw backlight interface, raw backlight interfaces registered
 * by the drm driver will have the drm-connector as their parent, check the
 * drm-connector's enabled sysfs attribute so that we pick the right LCD-panel
 * connector on laptops with hybrid-gfx. Fall back to just picking the first
 * raw backlight interface if no enabled interface is found.
 */
static gchar *
gsd_backlight_helper_get_raw (GList *devices)
{
	GUdevDevice *parent;
	const gchar *attr;
	GList *d;

	for (d = devices; d != NULL; d = d->next) {
		attr = g_udev_device_get_sysfs_attr (d->data, "type");
		if (g_strcmp0 (attr, "raw") != 0)
			continue;

		parent = g_udev_device_get_parent (d->data);
		if (!parent)
			continue;

		attr = g_udev_device_get_sysfs_attr (parent, "enabled");
		if (!attr || g_strcmp0 (attr, "enabled") != 0)
			continue;

		return g_strdup (g_udev_device_get_sysfs_path (d->data));
	}

	return gsd_backlight_helper_get_type (devices, "raw");
}

#endif /* HAVE_GUDEV */

char *
gsd_backlight_helper_get_best_backlight (GsdBacklightType *type)
{
#if HAVE_GUDEV
	gchar *path = NULL;
	GList *devices;
	GUdevClient *client;

	client = g_udev_client_new (NULL);
	devices = g_udev_client_query_by_subsystem (client, "backlight");
	if (devices == NULL)
		goto out;

	/* search the backlight devices and prefer the types:
	 * firmware -> platform -> raw */
	path = gsd_backlight_helper_get_type (devices, "firmware");
	if (path != NULL) {
		if (type)
			*type = GSD_BACKLIGHT_TYPE_FIRMWARE;
		goto out;
	}
	path = gsd_backlight_helper_get_type (devices, "platform");
	if (path != NULL) {
		if (type)
			*type = GSD_BACKLIGHT_TYPE_PLATFORM;
		goto out;
	}
	path = gsd_backlight_helper_get_raw (devices);
	if (path != NULL) {
		if (type)
			*type = GSD_BACKLIGHT_TYPE_RAW;
		goto out;
	}
out:
	g_object_unref (client);
	g_list_foreach (devices, (GFunc) g_object_unref, NULL);
	g_list_free (devices);
	return path;
#endif /* HAVE_GUDEV */

	return NULL;
}
