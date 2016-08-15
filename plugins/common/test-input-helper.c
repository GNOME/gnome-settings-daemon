/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Bastien Nocera <hadess@hadess.net>
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

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#include <sys/types.h>
#include <X11/Xatom.h>

#include "gsd-input-helper.h"
#include "gsd-device-manager.h"

int main (int argc, char **argv)
{
        GList *devices, *l;
	gboolean has_touchpad, has_touchscreen;
        gboolean has_mouse;

	gtk_init (&argc, &argv);

	has_mouse = mouse_is_present ();
	g_print ("Has mouse:\t\t\t\t%s\n", has_mouse ? "yes" : "no");

	has_touchpad = touchpad_is_present ();
	g_print ("Has touchpad:\t\t\t\t%s\n", has_touchpad ? "yes" : "no");

	has_touchscreen = touchscreen_is_present ();
	g_print ("Has touchscreen:\t\t\t%s\n", has_touchscreen ? "yes" : "no");

        devices = gsd_device_manager_list_devices (gsd_device_manager_get (), GSD_DEVICE_TYPE_MOUSE);
        for (l = devices; l != NULL; l = l->next)
                g_print ("Device '%s' is a mouse\n", gsd_device_get_name (l->data));
        g_list_free (devices);

        devices = gsd_device_manager_list_devices (gsd_device_manager_get (), GSD_DEVICE_TYPE_KEYBOARD);
        for (l = devices; l != NULL; l = l->next)
                g_print ("Device '%s' is a keyboard\n", gsd_device_get_name (l->data));
        g_list_free (devices);

        devices = gsd_device_manager_list_devices (gsd_device_manager_get (), GSD_DEVICE_TYPE_TOUCHPAD);
        for (l = devices; l != NULL; l = l->next)
                g_print ("Device '%s' is a touchpad\n", gsd_device_get_name (l->data));
        g_list_free (devices);

        devices = gsd_device_manager_list_devices (gsd_device_manager_get (), GSD_DEVICE_TYPE_TABLET);
        for (l = devices; l != NULL; l = l->next)
                g_print ("Device '%s' is a tablet\n", gsd_device_get_name (l->data));
        g_list_free (devices);

        devices = gsd_device_manager_list_devices (gsd_device_manager_get (), GSD_DEVICE_TYPE_TOUCHSCREEN);
        for (l = devices; l != NULL; l = l->next)
                g_print ("Device '%s' is a touchscreen\n", gsd_device_get_name (l->data));
        g_list_free (devices);

	return 0;
}
