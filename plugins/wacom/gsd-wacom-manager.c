/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2010 Red Hat, Inc.
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
 */

#include "config.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <locale.h>

#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include "gsd-enums.h"
#include "gsd-input-helper.h"
#include "gnome-settings-profile.h"
#include "gsd-wacom-manager.h"
#include "gsd-wacom-device.h"

/* Maximum number of buttons map-able. */
#define WACOM_MAX_BUTTONS 32

#define GSD_WACOM_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_WACOM_MANAGER, GsdWacomManagerPrivate))

#define KEY_ROTATION            "rotation"
#define KEY_TOUCH               "touch"
#define KEY_TPCBUTTON           "tablet-pc-button"
#define KEY_IS_ABSOLUTE         "is-absolute"
#define KEY_AREA                "area"
#define KEY_PAD_BUTTON_MAPPING  "pad-buttonmapping"

/* Stylus and Eraser settings */
#define KEY_BUTTON_MAPPING      "buttonmapping"
#define KEY_PRESSURETHRESHOLD   "pressurethreshold"
#define KEY_PRESSURECURVE       "pressurecurve"

/* See "Wacom Pressure Threshold" */
#define DEFAULT_PRESSURE_THRESHOLD 27

struct GsdWacomManagerPrivate
{
        guint start_idle_id;
        GdkDeviceManager *device_manager;
        guint device_added_id;
        guint device_removed_id;
        GHashTable *devices;
};

static void     gsd_wacom_manager_class_init  (GsdWacomManagerClass *klass);
static void     gsd_wacom_manager_init        (GsdWacomManager      *wacom_manager);
static void     gsd_wacom_manager_finalize    (GObject              *object);

G_DEFINE_TYPE (GsdWacomManager, gsd_wacom_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static GObject *
gsd_wacom_manager_constructor (GType                     type,
                               guint                      n_construct_properties,
                               GObjectConstructParam     *construct_properties)
{
        GsdWacomManager      *wacom_manager;

        wacom_manager = GSD_WACOM_MANAGER (G_OBJECT_CLASS (gsd_wacom_manager_parent_class)->constructor (type,
                                                                                                         n_construct_properties,
                                                                                                         construct_properties));

        return G_OBJECT (wacom_manager);
}

static void
gsd_wacom_manager_dispose (GObject *object)
{
        G_OBJECT_CLASS (gsd_wacom_manager_parent_class)->dispose (object);
}

static void
gsd_wacom_manager_class_init (GsdWacomManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->constructor = gsd_wacom_manager_constructor;
        object_class->dispose = gsd_wacom_manager_dispose;
        object_class->finalize = gsd_wacom_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdWacomManagerPrivate));
}

static XDevice *
open_device (GsdWacomDevice *device)
{
	XDevice *xdev;
	GdkDevice *gdk_device;
	int id;

	g_object_get (device, "gdk-device", &gdk_device, NULL);
	if (gdk_device == NULL)
		return NULL;
	g_object_get (gdk_device, "device-id", &id, NULL);

	gdk_error_trap_push ();
	xdev = XOpenDevice (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), id);
	if (gdk_error_trap_pop () || (xdev == NULL))
		return NULL;

	return xdev;
}


static void
wacom_set_property (GsdWacomDevice *device,
		    PropertyHelper *property)
{
	XDevice *xdev;

	xdev = open_device (device);
	device_set_property (xdev, gsd_wacom_device_get_tool_name (device), property);
	XCloseDevice (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), xdev);
}

static void
set_rotation (GsdWacomDevice *device,
	      GsdWacomRotation rotation)
{
        gchar rot = rotation;
        PropertyHelper property = {
                .name = "Wacom Rotation",
                .nitems = 1,
                .format = 8,
                .data.c = &rot,
        };

        wacom_set_property (device, &property);
}

static void
set_pressurecurve (GsdWacomDevice *device,
                   GVariant       *value)
{
        PropertyHelper property = {
                .name = "Wacom Pressurecurve",
                .nitems = 4,
                .format = 32,
        };
        gsize nvalues;

        property.data.i = g_variant_get_fixed_array (value, &nvalues, sizeof (gint32));

        if (nvalues != 4) {
                g_error ("Pressurecurve requires 4 values.");
                return;
        }

        wacom_set_property (device, &property);
}

/* Area handling. Each area is defined as top x/y, bottom x/y and limits the
 * usable area of the physical device to the given area (in device coords)
 */
static void
set_area (GsdWacomDevice  *device,
          GVariant        *value)
{
        PropertyHelper property = {
                .name = "Wacom Tablet Area",
                .nitems = 4,
                .format = 32,
        };
        gsize nvalues;

        property.data.i = g_variant_get_fixed_array (value, &nvalues, sizeof (gint32));

        if (nvalues != 4) {
                g_error ("Area configuration requires 4 values.");
                return;
        }

        wacom_set_property (device, &property);
}

static void
set_absolute (GsdWacomDevice  *device,
              gint             is_absolute)
{
	XDevice *xdev;

	xdev = open_device (device);
	gdk_error_trap_push ();
	XSetDeviceMode (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), xdev, is_absolute ? Absolute : Relative);
	if (gdk_error_trap_pop ())
		g_error ("Failed to set mode \"%s\" for \"%s\".",
			 is_absolute ? "Absolute" : "Relative", gsd_wacom_device_get_tool_name (device));
	XCloseDevice (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), xdev);
}

static void
set_device_buttonmap (GsdWacomDevice *device,
                      GVariant       *value)
{
	XDevice *xdev;
	gsize nmap;
	const gint *intmap;
	unsigned char map[WACOM_MAX_BUTTONS] = {};
	int i, j, rc;

	xdev = open_device (device);

	intmap = g_variant_get_fixed_array (value, &nmap, sizeof (gint32));
	for (i = 0; i < nmap && i < sizeof (map); i++)
		map[i] = intmap[i];

	gdk_error_trap_push ();

	/* X refuses to change the mapping while buttons are engaged,
	 * so if this is the case we'll retry a few times
	 */
	for (j = 0;
	     j < 20 && (rc = XSetDeviceButtonMapping (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), xdev, map, nmap)) == MappingBusy;
	     ++j) {
		g_usleep (300);
	}

	if (gdk_error_trap_pop () || rc != Success)
		g_warning ("Error in setting button mapping for \"%s\"", gsd_wacom_device_get_tool_name (device));

	XCloseDevice (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), xdev);
}

static void
set_touch (GsdWacomDevice *device,
	   gboolean        touch)
{
        gchar data = touch;
        PropertyHelper property = {
                .name = "Wacom Enable Touch",
                .nitems = 1,
                .format = 8,
                .data.c = &data,
        };

        wacom_set_property (device, &property);
}

static void
set_tpcbutton (GsdWacomDevice *device,
	       gboolean        tpcbutton)
{
        /* Wacom's TPCButton option which this setting emulates is to enable
         * Tablet PC stylus behaviour when on. The property "Hover Click"
         * works the other way round, i.e. if Hover Click is enabled this
         * is the equivalent of TPC behaviour disabled. */
        gchar data = tpcbutton ? 0 : 1;
        PropertyHelper property = {
                .name = "Wacom Hover Click",
                .nitems = 1,
                .format = 8,
                .data.c = &data,
        };

        wacom_set_property (device, &property);
}

static void
set_pressurethreshold (GsdWacomDevice *device,
                       gint            threshold)
{
        PropertyHelper property = {
                .name = "Wacom Pressure Threshold",
                .nitems = 1,
                .format = 32,
                .data.i = &threshold,
        };

        wacom_set_property (device, &property);
}

static void
set_wacom_settings (GsdWacomManager *manager,
		    GsdWacomDevice  *device)
{
	GsdWacomDeviceType type;
	GSettings *settings;

	g_debug ("Applying settings for device '%s' (type: %s)",
		 gsd_wacom_device_get_tool_name (device),
		 gsd_wacom_device_type_to_string (gsd_wacom_device_get_device_type (device)));

	settings = gsd_wacom_device_get_settings (device);
        set_rotation (device, g_settings_get_enum (settings, KEY_ROTATION));
        set_touch (device, g_settings_get_boolean (settings, KEY_TOUCH));

        type = gsd_wacom_device_get_device_type (device);

	if (type == WACOM_TYPE_CURSOR) {
		GVariant *values[4], *variant;
		guint i;

		set_absolute (device, FALSE);

		for (i = 0; i < G_N_ELEMENTS (values); i++)
			values[i] = g_variant_new_int32 (-1);

		variant = g_variant_new_array (G_VARIANT_TYPE_INT32, values, G_N_ELEMENTS (values));
		set_area (device, variant);
		g_variant_unref (variant);
		return;
	}

	if (type == WACOM_TYPE_PAD) {
		set_device_buttonmap (device, g_settings_get_value (settings, KEY_PAD_BUTTON_MAPPING));
		return;
	}

	if (type == WACOM_TYPE_STYLUS)
		set_tpcbutton (device, g_settings_get_boolean (settings, KEY_TPCBUTTON));

	set_absolute (device, g_settings_get_boolean (settings, KEY_IS_ABSOLUTE));
	set_area (device, g_settings_get_value (settings, KEY_AREA));

        /* only pen and eraser have pressure threshold and curve settings */
        if (type == WACOM_TYPE_STYLUS ||
            type == WACOM_TYPE_ERASER) {
		GSettings *stylus_settings;
		GsdWacomStylus *stylus;

		g_object_get (device, "last-stylus", &stylus, NULL);
		if (stylus != NULL) {
			int threshold;

			stylus_settings = gsd_wacom_stylus_get_settings (stylus);
			set_pressurecurve (device, g_settings_get_value (stylus_settings, KEY_PRESSURECURVE));
			set_device_buttonmap (device, g_settings_get_value (stylus_settings, KEY_BUTTON_MAPPING));

			threshold = g_settings_get_int (stylus_settings, KEY_PRESSURETHRESHOLD);
			if (threshold == -1)
				threshold = DEFAULT_PRESSURE_THRESHOLD;
			set_pressurethreshold (device, threshold);
		}
	}
}

static void
wacom_settings_changed (GSettings      *settings,
			gchar          *key,
			GsdWacomDevice *device)
{
	GsdWacomDeviceType type;

	type = gsd_wacom_device_get_device_type (device);

	if (g_str_equal (key, KEY_ROTATION)) {
		set_rotation (device, g_settings_get_enum (settings, key));
	} else if (g_str_equal (key, KEY_TOUCH)) {
		set_touch (device, g_settings_get_boolean (settings, key));
	} else if (g_str_equal (key, KEY_TPCBUTTON)) {
		set_tpcbutton (device, g_settings_get_boolean (settings, key));
	} else if (g_str_equal (key, KEY_IS_ABSOLUTE)) {
		if (type != WACOM_TYPE_CURSOR &&
		    type != WACOM_TYPE_PAD)
			set_absolute (device, g_settings_get_boolean (settings, key));
	} else if (g_str_equal (key, KEY_AREA)) {
		if (type != WACOM_TYPE_CURSOR &&
		    type != WACOM_TYPE_PAD)
			set_area (device, g_settings_get_value (settings, key));
	} else if (g_str_equal (key, KEY_PAD_BUTTON_MAPPING)) {
		if (type == WACOM_TYPE_PAD)
			set_device_buttonmap (device, g_settings_get_value (settings, key));
	} else {
		g_warning ("Unhandled tablet-wide setting '%s' changed", key);
	}
}

static void
stylus_settings_changed (GSettings      *settings,
			 gchar          *key,
			 GsdWacomStylus *stylus)
{
	GsdWacomDevice *device;
	GsdWacomStylus *last_stylus;

	device = gsd_wacom_stylus_get_device (stylus);

	g_object_get (device, "last-stylus", &last_stylus, NULL);
	if (last_stylus != stylus) {
		g_debug ("Not applying changed settings because '%s' is the current stylus, not '%s'",
			 last_stylus ? gsd_wacom_stylus_get_name (last_stylus) : "NONE",
			 gsd_wacom_stylus_get_name (stylus));
		return;
	}

	if (g_str_equal (key, KEY_PRESSURECURVE)) {
		set_pressurecurve (device, g_settings_get_value (settings, key));
	} else if (g_str_equal (key, KEY_PRESSURETHRESHOLD)) {
		int threshold;

		threshold = g_settings_get_int (settings, KEY_PRESSURETHRESHOLD);
		if (threshold == -1)
			threshold = DEFAULT_PRESSURE_THRESHOLD;
		set_pressurethreshold (device, threshold);
	} else if (g_str_equal (key, KEY_BUTTON_MAPPING)) {
		set_device_buttonmap (device, g_settings_get_value (settings, key));
	}  else {
		g_warning ("Unhandled stylus setting '%s' changed", key);
	}
}

static void
device_added_cb (GdkDeviceManager *device_manager,
                 GdkDevice        *gdk_device,
                 GsdWacomManager  *manager)
{
	GsdWacomDevice *device;
	GSettings *settings;

	device = gsd_wacom_device_new (gdk_device);
	if (gsd_wacom_device_get_device_type (device) == WACOM_TYPE_INVALID) {
		g_object_unref (device);
		return;
	}
	g_debug ("Adding device '%s' (type: '%s') to known devices list",
		 gsd_wacom_device_get_tool_name (device),
		 gsd_wacom_device_type_to_string (gsd_wacom_device_get_device_type (device)));
	g_hash_table_insert (manager->priv->devices, (gpointer) gdk_device, device);

	settings = gsd_wacom_device_get_settings (device);
	g_signal_connect (G_OBJECT (settings), "changed",
			  G_CALLBACK (wacom_settings_changed), device);

	if (gsd_wacom_device_get_device_type (device) == WACOM_TYPE_STYLUS) {
		GList *styli, *l;

		styli = gsd_wacom_device_list_styli (device);

		for (l = styli ; l ; l = l->next) {
			settings = gsd_wacom_stylus_get_settings (l->data);
			g_signal_connect (G_OBJECT (settings), "changed",
					  G_CALLBACK (stylus_settings_changed), l->data);
		}

		g_list_free (styli);
	}

        set_wacom_settings (manager, device);
}

static void
device_removed_cb (GdkDeviceManager *device_manager,
                   GdkDevice        *gdk_device,
                   GsdWacomManager  *manager)
{
	g_debug ("Removing device '%s' from known devices list",
		 gdk_device_get_name (gdk_device));
	g_hash_table_remove (manager->priv->devices, gdk_device);
}

static void
set_devicepresence_handler (GsdWacomManager *manager)
{
        GdkDeviceManager *device_manager;

        device_manager = gdk_display_get_device_manager (gdk_display_get_default ());
        if (device_manager == NULL)
                return;

        manager->priv->device_added_id = g_signal_connect (G_OBJECT (device_manager), "device-added",
                                                           G_CALLBACK (device_added_cb), manager);
        manager->priv->device_removed_id = g_signal_connect (G_OBJECT (device_manager), "device-removed",
                                                             G_CALLBACK (device_removed_cb), manager);
        manager->priv->device_manager = device_manager;
}

static void
gsd_wacom_manager_init (GsdWacomManager *manager)
{
        manager->priv = GSD_WACOM_MANAGER_GET_PRIVATE (manager);
}

static gboolean
gsd_wacom_manager_idle_cb (GsdWacomManager *manager)
{
	GList *devices, *l;

        gnome_settings_profile_start (NULL);

        manager->priv->devices = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);

        set_devicepresence_handler (manager);

        devices = gdk_device_manager_list_devices (manager->priv->device_manager, GDK_DEVICE_TYPE_SLAVE);
        for (l = devices; l ; l = l->next)
		device_added_cb (manager->priv->device_manager, l->data, manager);
        g_list_free (devices);

        gnome_settings_profile_end (NULL);

        manager->priv->start_idle_id = 0;

        return FALSE;
}

gboolean
gsd_wacom_manager_start (GsdWacomManager *manager,
                         GError         **error)
{
        gnome_settings_profile_start (NULL);

        if (supports_xinput2_devices (NULL) == FALSE) {
                g_debug ("No Xinput2 support, disabling plugin");
                return TRUE;
        }

        manager->priv->start_idle_id = g_idle_add ((GSourceFunc) gsd_wacom_manager_idle_cb, manager);

        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_wacom_manager_stop (GsdWacomManager *manager)
{
        GsdWacomManagerPrivate *p = manager->priv;

        g_debug ("Stopping wacom manager");

        if (p->device_manager != NULL) {
                g_signal_handler_disconnect (p->device_manager, p->device_added_id);
                g_signal_handler_disconnect (p->device_manager, p->device_removed_id);
                p->device_manager = NULL;
        }
}

static void
gsd_wacom_manager_finalize (GObject *object)
{
        GsdWacomManager *wacom_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_WACOM_MANAGER (object));

        wacom_manager = GSD_WACOM_MANAGER (object);

        g_return_if_fail (wacom_manager->priv != NULL);

        if (wacom_manager->priv->devices) {
                g_hash_table_destroy (wacom_manager->priv->devices);
                wacom_manager->priv->devices = NULL;
        }

        if (wacom_manager->priv->start_idle_id != 0)
                g_source_remove (wacom_manager->priv->start_idle_id);

        G_OBJECT_CLASS (gsd_wacom_manager_parent_class)->finalize (object);
}

GsdWacomManager *
gsd_wacom_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_WACOM_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_WACOM_MANAGER (manager_object);
}
