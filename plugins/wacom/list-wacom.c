/*
 * Copyright (C) 2011 Red Hat, Inc.
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Author: Bastien Nocera <hadess@hadess.net>
 *
 */

#include "config.h"

#include <gtk/gtk.h>

#include "gsd-wacom-device.h"

int main (int argc, char **argv)
{
	GdkDeviceManager *mgr;
	GList *list, *l;

	gtk_init (&argc, &argv);

	mgr = gdk_display_get_device_manager (gdk_display_get_default ());

	list = gdk_device_manager_list_devices (mgr, GDK_DEVICE_TYPE_SLAVE);
	for (l = list; l ; l = l->next) {
		GsdWacomDevice *device;

		device = gsd_wacom_device_new (l->data);
		g_message ("Device '%s'", gsd_wacom_device_get_name (device));
		/* FIXME print properties */
		if (gsd_wacom_device_get_device_type (device) == WACOM_TYPE_STYLUS) {
			GList *styli, *j;

			styli = gsd_wacom_device_list_styli (device);
			for (j = styli; j; j = j->next) {
				g_message ("\tStylus '%s'", gsd_wacom_stylus_get_name (j->data));
				/* FIXME print properties */
			}
			g_list_free (styli);
		}
		g_object_unref (device);
	}
	g_list_free (list);

	return 0;
}
