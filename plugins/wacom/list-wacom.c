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

static char *
get_loc (GSettings *settings)
{
	char *path, *schema, *ret;

	g_return_val_if_fail (G_IS_SETTINGS (settings), NULL);

	g_object_get (G_OBJECT (settings),
		      "path", &path,
		      "schema", &schema,
		      NULL);
	ret = g_strdup_printf ("schema: %s (path: %s)", schema, path);
	g_free (schema);
	g_free (path);

	return ret;
}

#define BOOL_AS_STR(x) (x ? "yes" : "no")

static void
list_devices (GList *devices)
{
	GList *l;

	for (l = devices; l ; l = l->next) {
		GsdWacomDevice *device;
		GsdWacomDeviceType type;
		char *loc;

		device = l->data;

		g_print ("Device '%s' (type: %s)\n",
			 gsd_wacom_device_get_name (device),
			 gsd_wacom_device_type_to_string (gsd_wacom_device_get_device_type (device)));
		g_print ("\tReversible: %s\n", BOOL_AS_STR (gsd_wacom_device_reversible (device)));
		g_print ("\tScreen Tablet: %s\n", BOOL_AS_STR (gsd_wacom_device_is_screen_tablet (device)));

		loc = get_loc (gsd_wacom_device_get_settings (device));
		g_print ("\tGeneric settings: %s\n", loc);
		g_free (loc);

		type = gsd_wacom_device_get_device_type (device);
		if (type == WACOM_TYPE_STYLUS ||
		    type == WACOM_TYPE_ERASER) {
			GList *styli, *j;
			GsdWacomStylus *current_stylus;

			g_object_get (device, "last-stylus", &current_stylus, NULL);

			styli = gsd_wacom_device_list_styli (device);
			for (j = styli; j; j = j->next) {
				GsdWacomStylus *stylus;

				stylus = j->data;
				g_print ("\t%sStylus: '%s'\n",
					 current_stylus == stylus ? "*** " : "",
					 gsd_wacom_stylus_get_name (stylus));

				loc = get_loc (gsd_wacom_stylus_get_settings (stylus));
				g_print ("\t\tSettings: %s\n", loc);
				g_free (loc);

				g_print ("\t\tIcon name: %s\n", gsd_wacom_stylus_get_icon_name (stylus));
			}
			g_list_free (styli);
		}
		g_object_unref (device);
	}
	g_list_free (devices);
}

static void
list_actual_devices (void)
{
	GdkDeviceManager *mgr;
	GList *list, *l, *devices;

	mgr = gdk_display_get_device_manager (gdk_display_get_default ());

	list = gdk_device_manager_list_devices (mgr, GDK_DEVICE_TYPE_SLAVE);
	devices = NULL;
	for (l = list; l ; l = l->next) {
		GsdWacomDevice *device;

		device = gsd_wacom_device_new (l->data);
		if (gsd_wacom_device_get_device_type (device) == WACOM_TYPE_INVALID) {
			g_object_unref (device);
			continue;
		}

		devices = g_list_prepend (devices, device);
	}
	g_list_free (list);

	list_devices (devices);
}

static void
list_fake_devices (void)
{
	GList *devices;

	devices = gsd_wacom_device_create_fake_cintiq ();
	list_devices (devices);

	devices = gsd_wacom_device_create_fake_bt ();
	list_devices (devices);

	devices = gsd_wacom_device_create_fake_x201 ();
	list_devices (devices);
}

int main (int argc, char **argv)
{
	gtk_init (&argc, &argv);

	list_actual_devices ();
//	list_fake_devices ();

	return 0;
}
