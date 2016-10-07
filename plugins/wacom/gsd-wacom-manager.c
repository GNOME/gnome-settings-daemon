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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
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
#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <libnotify/notify.h>
#include <X11/Xatom.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <Xwacom.h>
#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-rr.h>

#include "gsd-enums.h"
#include "gsd-input-helper.h"
#include "gsd-keygrab.h"
#include "gnome-settings-plugin.h"
#include "gnome-settings-profile.h"
#include "gnome-settings-bus.h"
#include "gsd-wacom-manager.h"
#include "gsd-wacom-device.h"
#include "gsd-wacom-oled.h"
#include "gsd-wacom-osd-window.h"
#include "gsd-shell-helper.h"
#include "gsd-device-mapper.h"
#include "gsd-device-manager.h"
#include "gsd-device-manager-x11.h"

#define GSD_WACOM_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_WACOM_MANAGER, GsdWacomManagerPrivate))

#define KEY_ROTATION            "rotation"
#define KEY_TOUCH               "touch"
#define KEY_IS_ABSOLUTE         "is-absolute"
#define KEY_AREA                "area"
#define KEY_KEEP_ASPECT         "keep-aspect"

/* Stylus and Eraser settings */
#define KEY_BUTTON_MAPPING      "buttonmapping"
#define KEY_PRESSURETHRESHOLD   "pressurethreshold"
#define KEY_PRESSURECURVE       "pressurecurve"

/* Button settings */
#define KEY_ACTION_TYPE            "action-type"
#define KEY_CUSTOM_ACTION          "custom-action"
#define KEY_CUSTOM_ELEVATOR_ACTION "custom-elevator-action"
#define OLED_LABEL                 "oled-label"

/* See "Wacom Pressure Threshold" */
#define DEFAULT_PRESSURE_THRESHOLD 27

#define UNKNOWN_DEVICE_NOTIFICATION_TIMEOUT 15000

#define GSD_WACOM_DBUS_PATH GSD_DBUS_PATH "/Wacom"
#define GSD_WACOM_DBUS_NAME GSD_DBUS_NAME ".Wacom"

static const gchar introspection_xml[] =
"<node name='/org/gnome/SettingsDaemon/Wacom'>"
"  <interface name='org.gnome.SettingsDaemon.Wacom'>"
"    <method name='SetOSDVisibility'>"
"      <arg name='device_id' direction='in' type='u'/>"
"      <arg name='visible' direction='in' type='b'/>"
"      <arg name='edition_mode' direction='in' type='b'/>"
"    </method>"
"  </interface>"
"</node>";

struct GsdWacomManagerPrivate
{
        guint start_idle_id;
        GsdDeviceManager *device_manager;
        guint device_added_id;
        guint device_changed_id;
        guint device_removed_id;
        GHashTable *devices; /* key = GdkDevice, value = GsdWacomDevice */
        GnomeRRScreen *rr_screen;
        GHashTable *warned_devices;

        GsdShell *shell_proxy;

        GsdDeviceMapper *device_mapper;
        guint mapping_changed_id;

        /* button capture */
        GdkScreen *screen;
        int      opcode;

        /* Help OSD window */
        GtkWidget *osd_window;

        /* DBus */
        GDBusNodeInfo   *introspection_data;
        GDBusConnection *dbus_connection;
        GCancellable    *dbus_cancellable;
        guint            dbus_register_object_id;
        guint            name_id;
};

static void     gsd_wacom_manager_class_init  (GsdWacomManagerClass *klass);
static void     gsd_wacom_manager_init        (GsdWacomManager      *wacom_manager);
static void     gsd_wacom_manager_finalize    (GObject              *object);

static gboolean osd_window_toggle_visibility (GsdWacomManager *manager,
                                              GsdWacomDevice  *device);

static GsdWacomDevice * device_id_to_device (GsdWacomManager *manager,
                                             int              deviceid);

static void             osd_window_destroy  (GsdWacomManager *manager);

G_DEFINE_TYPE (GsdWacomManager, gsd_wacom_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static void
gsd_wacom_manager_class_init (GsdWacomManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_wacom_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdWacomManagerPrivate));
}

static int
get_device_id (GsdWacomDevice *device)
{
	GdkDevice *gdk_device;
	int id;

	gdk_device = gsd_wacom_device_get_gdk_device (device);

	if (gdk_device == NULL)
		return -1;
	g_object_get (gdk_device, "device-id", &id, NULL);
	return id;
}

static XDevice *
open_device (GsdWacomDevice *device)
{
	XDevice *xdev;
	int id;

	id = get_device_id (device);
	if (id < 0)
		return NULL;

	gdk_error_trap_push ();
	xdev = XOpenDevice (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), id);
	if (gdk_error_trap_pop () || (xdev == NULL))
		return NULL;

	return xdev;
}

static gboolean
set_osd_visibility (GsdWacomManager *manager,
		    guint            device_id,
		    gboolean         visible,
		    gboolean         edition_mode)
{
	GsdWacomDevice *device;
	gboolean ret = TRUE;

	if (!visible) {
		osd_window_destroy (manager);
		return TRUE;
	}

	device = device_id_to_device (manager, device_id);
	if (!device)
		return FALSE;

	/* Check whether we need to destroy the old OSD window */
	if (manager->priv->osd_window &&
	    device != gsd_wacom_osd_window_get_device (GSD_WACOM_OSD_WINDOW (manager->priv->osd_window)))
		osd_window_destroy (manager);

	/* Do we need to create a new OSD or are we reusing one? */
	if (manager->priv->osd_window == NULL)
		ret = osd_window_toggle_visibility (manager, device);

	gsd_wacom_osd_window_set_edition_mode (GSD_WACOM_OSD_WINDOW (manager->priv->osd_window),
					       edition_mode);

	return ret;
}

static void
handle_method_call (GDBusConnection       *connection,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *interface_name,
                    const gchar           *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               data)
{
	GsdWacomManager *self = GSD_WACOM_MANAGER (data);

	if (g_strcmp0 (method_name, "SetOSDVisibility") == 0) {
		guint32		   device_id;
		gboolean	   visible, edition_mode;

		g_variant_get (parameters, "(ubb)", &device_id, &visible, &edition_mode);
		if (!set_osd_visibility (self, device_id, visible, edition_mode)) {
			g_dbus_method_invocation_return_error_literal (invocation,
								       G_IO_ERROR,
								       G_IO_ERROR_FAILED,
								       "Failed to show the OSD for this device");
		} else {
			g_dbus_method_invocation_return_value (invocation, NULL);
		}
	}
}

static const GDBusInterfaceVTable interface_vtable =
{
	handle_method_call,
	NULL, /* Get Property */
	NULL, /* Set Property */
};

static void
wacom_set_property (GsdWacomDevice *device,
		    PropertyHelper *property)
{
	XDevice *xdev;

	xdev = open_device (device);
	if (xdev == NULL)
		return;
	device_set_property (xdev, gsd_wacom_device_get_tool_name (device), property);
	xdevice_close (xdev);
}

static void
set_rotation (GsdWacomDevice   *device,
	      GsdWacomRotation	rotation)
{
	gchar rot = rotation;
	PropertyHelper property = {
		.name = "Wacom Rotation",
		.nitems = 1,
		.format = 8,
		.type = XA_INTEGER,
		.data.c = &rot
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
                .type   = XA_INTEGER,
                .format = 32,
        };
        gsize nvalues;

        property.data.i = g_variant_get_fixed_array (value, &nvalues, sizeof (gint32));

        if (nvalues != 4) {
                g_error ("Pressurecurve requires 4 values.");
                return;
        }

        wacom_set_property (device, &property);
        g_variant_unref (value);
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
                .type   = XA_INTEGER,
                .format = 32,
        };
        gsize nvalues;

        property.data.i = g_variant_get_fixed_array (value, &nvalues, sizeof (gint32));

        if (nvalues != 4) {
                g_error ("Area configuration requires 4 values.");
                g_variant_unref (value);
                return;
        }

        if (property.data.i[0] == -1 &&
            property.data.i[1] == -1 &&
            property.data.i[2] == -1 &&
            property.data.i[3] == -1) {
                gint *area;
                area = gsd_wacom_device_get_default_area (device);

                if (!area) {
                        g_warning ("No default area could be obtained from the device");
                        g_variant_unref (value);
                        return;
                }

                property.data.i = area;
                g_debug ("Resetting area to: %d, %d, %d, %d",
                         property.data.i[0],
                         property.data.i[1],
                         property.data.i[2],
                         property.data.i[3]);
                wacom_set_property (device, &property);
                g_free (area);
        } else {
                g_debug ("Setting area to: %d, %d, %d, %d",
                         property.data.i[0],
                         property.data.i[1],
                         property.data.i[2],
                         property.data.i[3]);
                wacom_set_property (device, &property);
        }
        g_variant_unref (value);
}

static void
reset_area (GsdWacomDevice *device)
{
        GVariant *values[4], *variant;
        guint i;

        /* Set area to default values for the device */
        for (i = 0; i < G_N_ELEMENTS (values); i++)
                values[i] = g_variant_new_int32 (-1);
        variant = g_variant_new_array (G_VARIANT_TYPE_INT32, values, G_N_ELEMENTS (values));

        set_area (device, variant);
}

static void
set_absolute (GsdWacomDevice  *device,
              gint             is_absolute)
{
	XDevice *xdev;

	xdev = open_device (device);
	if (xdev == NULL)
		return;
	gdk_error_trap_push ();
	XSetDeviceMode (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), xdev, is_absolute ? Absolute : Relative);
	if (gdk_error_trap_pop ())
		g_warning ("Failed to set mode \"%s\" for \"%s\".",
			   is_absolute ? "Absolute" : "Relative", gsd_wacom_device_get_tool_name (device));
	xdevice_close (xdev);
}

static void
compute_aspect_area (gint monitor,
                     gint *area,
                     GsdWacomRotation rotation)
{
	gint width  = area[2] - area[0];
	gint height = area[3] - area[1];
	GdkScreen *screen;
	GdkRectangle monitor_geometry;
	float aspect;

	screen = gdk_screen_get_default ();
	if (monitor < 0) {
		monitor_geometry.width = gdk_screen_get_width (screen);
		monitor_geometry.height = gdk_screen_get_height (screen);
	} else {
		gdk_screen_get_monitor_geometry (screen, monitor, &monitor_geometry);
	}

	if (rotation == GSD_WACOM_ROTATION_CW || rotation == GSD_WACOM_ROTATION_CCW)
		aspect = (float) monitor_geometry.height / (float) monitor_geometry.width;
	else
		aspect = (float) monitor_geometry.width / (float) monitor_geometry.height;

	if ((float) width / (float) height > aspect)
		width = height * aspect;
	else
		height = width / aspect;

	switch (rotation)
	{
		case GSD_WACOM_ROTATION_NONE:
			area[2] = area[0] + width;
			area[3] = area[1] + height;
			break;
		case GSD_WACOM_ROTATION_CW:
			area[0] = area[2] - width;
			area[3] = area[1] + height;
			break;
		case GSD_WACOM_ROTATION_HALF:
			area[0] = area[2] - width;
			area[1] = area[3] - height;
			break;
		case GSD_WACOM_ROTATION_CCW:
			area[2] = area[0] + width;
			area[1] = area[3] - height;
			break;
		default:
			break;
	}
}

static void
set_keep_aspect (GsdWacomDevice *device,
                 gboolean        keep_aspect)
{
        GVariant *values[4], *variant;
	guint i;
	GdkDevice *gdk_device;
	GsdDeviceMapper *mapper;
        GsdDeviceManager *device_manager;
        GsdDevice *gsd_device;
	gint *area;
	gint monitor = GSD_WACOM_SET_ALL_MONITORS;
	GsdWacomRotation rotation;
	GSettings *settings;

        settings = gsd_wacom_device_get_settings (device);

        /* Set area to default values for the device */
	for (i = 0; i < G_N_ELEMENTS (values); i++)
		values[i] = g_variant_new_int32 (-1);
	variant = g_variant_new_array (G_VARIANT_TYPE_INT32, values, G_N_ELEMENTS (values));

        /* If keep_aspect is not set, just reset the area to default and let
         * gsettings notification call reset_area() for us...
         */
	if (!keep_aspect) {
		g_settings_set_value (settings, KEY_AREA, variant);
		return;
        }
	g_variant_unref (variant);

        /* Reset the device area to get the default area */
	reset_area (device);

	/* Get current rotation */
	rotation = g_settings_get_enum (settings, KEY_ROTATION);

	/* Get current area */
	area = gsd_wacom_device_get_area (device);
	if (!area) {
		g_warning("Device area not available.\n");
		return;
	}

	/* Get corresponding monitor size */
	mapper = gsd_device_mapper_get ();
	device_manager = gsd_device_manager_get ();

	gdk_device = gsd_wacom_device_get_gdk_device (device);
	gsd_device = gsd_x11_device_manager_lookup_gdk_device (GSD_X11_DEVICE_MANAGER (device_manager),
							       gdk_device);
	monitor = gsd_device_mapper_get_device_monitor (mapper, gsd_device);

	/* Adjust area to match the monitor aspect ratio */
	g_debug ("Initial device area: (%d,%d) (%d,%d)", area[0], area[1], area[2], area[3]);
	compute_aspect_area (monitor, area, rotation);
	g_debug ("Adjusted device area: (%d,%d) (%d,%d)", area[0], area[1], area[2], area[3]);

	for (i = 0; i < G_N_ELEMENTS (values); i++)
		values[i] = g_variant_new_int32 (area[i]);
	variant = g_variant_new_array (G_VARIANT_TYPE_INT32, values, G_N_ELEMENTS (values));
	g_settings_set_value (settings, KEY_AREA, variant);

	g_free (area);
}


static void
set_device_buttonmap (GsdWacomDevice *device,
                      GVariant       *value)
{
	XDevice *xdev;
	gsize nmap;
	const gint *intmap;
	unsigned char *map;
	int i, j, rc;

	xdev = open_device (device);
	if (xdev == NULL) {
	        g_variant_unref (value);
		return;
	}

	intmap = g_variant_get_fixed_array (value, &nmap, sizeof (gint32));
	map = g_new0 (unsigned char, nmap);
	for (i = 0; i < nmap; i++)
		map[i] = intmap[i];
        g_variant_unref (value);

	gdk_error_trap_push ();

	/* X refuses to change the mapping while buttons are engaged,
	 * so if this is the case we'll retry a few times
	 */
	for (j = 0;
	     j < 20 && (rc = XSetDeviceButtonMapping (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), xdev, map, nmap)) == MappingBusy;
	     ++j) {
		g_usleep (300);
	}

	if ((gdk_error_trap_pop () && rc != MappingSuccess) ||
	    rc != MappingSuccess)
		g_warning ("Error in setting button mapping for \"%s\"", gsd_wacom_device_get_tool_name (device));

	g_free (map);

	xdevice_close (xdev);
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
                .type   = XA_INTEGER,
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
                .type   = XA_INTEGER,
                .data.i = &threshold,
        };

        wacom_set_property (device, &property);
}

static void
apply_stylus_settings (GsdWacomDevice *device)
{
	GSettings *stylus_settings;
	GsdWacomStylus *stylus;
	int threshold;

	g_object_get (device, "last-stylus", &stylus, NULL);
	if (stylus == NULL) {
		g_warning ("Last stylus is not set");
		return;
	}

	g_debug ("Applying setting for stylus '%s' on device '%s'",
		 gsd_wacom_stylus_get_name (stylus),
		 gsd_wacom_device_get_name (device));

	stylus_settings = gsd_wacom_stylus_get_settings (stylus);
	set_pressurecurve (device, g_settings_get_value (stylus_settings, KEY_PRESSURECURVE));
	set_device_buttonmap (device, g_settings_get_value (stylus_settings, KEY_BUTTON_MAPPING));

	threshold = g_settings_get_int (stylus_settings, KEY_PRESSURETHRESHOLD);
	if (threshold == -1)
		threshold = DEFAULT_PRESSURE_THRESHOLD;
	set_pressurethreshold (device, threshold);
}

static void
set_led (GsdWacomDevice       *device,
	 GsdWacomTabletButton *button,
	 int                   index)
{
	GError *error = NULL;
	const char *path;
	char *command;
	gint status_led;
	gboolean ret;

#ifndef HAVE_GUDEV
	/* Not implemented on non-Linux systems */
	return;
#endif
	g_return_if_fail (index >= 1);

	path = gsd_wacom_device_get_path (device);
	status_led = button->status_led;

	if (status_led == GSD_WACOM_NO_LED) {
		g_debug ("Ignoring unhandled group ID %d for device %s",
		         button->group_id, gsd_wacom_device_get_name (device));
		return;
	}
	g_debug ("Switching group ID %d to index %d for device %s", button->group_id, index, path);

	command = g_strdup_printf ("pkexec " LIBEXECDIR "/gsd-wacom-led-helper --path %s --group %d --led %d",
				   path, status_led, index - 1);
	ret = g_spawn_command_line_sync (command,
					 NULL,
					 NULL,
					 NULL,
					 &error);

	if (ret == FALSE) {
		g_debug ("Failed to launch '%s': %s", command, error->message);
		g_error_free (error);
	}

	g_free (command);
}

struct DefaultButtons {
	const char *button;
	int         num;
};

struct DefaultButtons def_touchrings_buttons[] = {
	/* Touchrings */
	{ "AbsWheelUp", 90 },
	{ "AbsWheelDown", 91 },
	{ "RelWheelUp", 90 },
	{ "RelWheelDown", 91 },
	{ "AbsWheel2Up", 92 },
	{ "AbsWheel2Down", 93 },
	{ NULL, 0 }
};

struct DefaultButtons def_touchstrip_buttons[] = {
	/* Touchstrips */
	{ "StripLeftUp", 94 },
	{ "StripLeftDown", 95 },
	{ "StripRightUp", 96 },
	{ "StripRightDown", 97 },
	{ NULL, 0 }
};

static void
reset_touch_buttons (XDevice               *xdev,
		     struct DefaultButtons *buttons,
		     const char            *device_property)
{
	Atom actions[6];
	Atom action_prop;
	guint i;

	/* Create a device property with the action for button i */
	for (i = 0; buttons[i].button != NULL; i++)
	{
		char *propname;
		glong action[2]; /* press + release */
		Atom prop;
		int mapped_button = buttons[i].num;

		action[0] = AC_BUTTON | AC_KEYBTNPRESS | mapped_button;
		action[1] = AC_BUTTON | mapped_button;

		propname = g_strdup_printf ("Button %s action", buttons[i].button);
		prop = XInternAtom (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), propname, False);
		g_free (propname);
		XChangeDeviceProperty (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), xdev,
				       prop, XA_INTEGER, 32, PropModeReplace,
				       (const guchar *) &action, 2);

		/* prop now contains press + release for the mapped button */
		actions[i] = prop;
	}

	/* Now set the actual action property to contain references to the various
	 * actions */
	action_prop = XInternAtom (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), device_property, True);
	XChangeDeviceProperty (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), xdev,
			       action_prop, XA_ATOM, 32, PropModeReplace,
			       (const guchar *) actions, i);
}

/* This function does LED and OLED */
static void
update_pad_leds (GsdWacomDevice *device)
{
	GList *buttons, *l;

	/* Reset all the LEDs and OLEDs*/
	buttons = gsd_wacom_device_get_buttons (device);
	for (l = buttons; l != NULL; l = l->next) {
		GsdWacomTabletButton *button = l->data;
		if (button->type == WACOM_TABLET_BUTTON_TYPE_HARDCODED &&
		    button->status_led != GSD_WACOM_NO_LED) {
			set_led (device, button, 1);
		}
		if (button->has_oled) {
			char *label;
			label = g_settings_get_string (button->settings, OLED_LABEL);
			set_oled (device, button->id, label);
			g_free (label);
		}
	}
	g_list_free (buttons);
}

static void
reset_pad_buttons (GsdWacomDevice *device)
{
	XDevice *xdev;
	int nmap;
	unsigned char *map;
	int i, j, rc;

	/* Normal buttons */
	xdev = open_device (device);
	if (xdev == NULL)
		return;

	gdk_error_trap_push ();

	nmap = 256;
	map = g_new0 (unsigned char, nmap);
	for (i = 0; i < nmap && i < sizeof (map); i++)
		map[i] = i + 1;

	/* X refuses to change the mapping while buttons are engaged,
	 * so if this is the case we'll retry a few times */
	for (j = 0;
	     j < 20 && (rc = XSetDeviceButtonMapping (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), xdev, map, nmap)) == MappingBusy;
	     ++j) {
		g_usleep (300);
	}

	if ((gdk_error_trap_pop () && rc != MappingSuccess) ||
	    rc != MappingSuccess)
		g_warning ("Error in resetting button mapping for \"%s\" (rc=%d)", gsd_wacom_device_get_tool_name (device), rc);

	g_free (map);

	gdk_error_trap_push ();
	reset_touch_buttons (xdev, def_touchrings_buttons, "Wacom Wheel Buttons");
	reset_touch_buttons (xdev, def_touchstrip_buttons, "Wacom Strip Buttons");
	gdk_error_trap_pop_ignored ();

	xdevice_close (xdev);

	update_pad_leds (device);
}

static void
gsettings_oled_changed (GSettings *settings,
			gchar *key,
			GsdWacomTabletButton *button)
{
	GsdWacomDevice *device;
	char *label;

	label = g_settings_get_string (settings, OLED_LABEL);
	device = g_object_get_data (G_OBJECT (button->settings), "parent-device");
	set_oled (device, button->id, label);
	g_free (label);
}

static void
set_wacom_settings (GsdWacomManager *manager,
		    GsdWacomDevice  *device)
{
	GsdWacomDeviceType type;
	GSettings *settings;
	GList *buttons, *l;

	g_debug ("Applying settings for device '%s' (type: %s)",
		 gsd_wacom_device_get_tool_name (device),
		 gsd_wacom_device_type_to_string (gsd_wacom_device_get_device_type (device)));

	settings = gsd_wacom_device_get_settings (device);
        set_touch (device, g_settings_get_boolean (settings, KEY_TOUCH));

        type = gsd_wacom_device_get_device_type (device);

	if (type == WACOM_TYPE_TOUCH &&
	    gsd_wacom_device_is_screen_tablet (device) == FALSE) {
		set_absolute (device, FALSE);
		return;
	}

	if (type == WACOM_TYPE_CURSOR) {
		set_absolute (device, FALSE);
		reset_area (device);
		return;
	}

	if (type == WACOM_TYPE_PAD) {
		int id;

		buttons = gsd_wacom_device_get_buttons (device);
		for (l = buttons; l != NULL; l = l->next) {
			GsdWacomTabletButton *button = l->data;
			if (button->has_oled) {
				g_signal_connect (G_OBJECT (button->settings), "changed::" OLED_LABEL,
						  G_CALLBACK (gsettings_oled_changed), button);
				g_object_set_data (G_OBJECT (button->settings), "parent-device", device);
			}
		}
		g_list_free (buttons);

		id = get_device_id (device);
		reset_pad_buttons (device);
		grab_button (id, TRUE, manager->priv->screen);

		return;
	} else {
		set_rotation (device, g_settings_get_enum (settings, KEY_ROTATION));
	}

	set_absolute (device, g_settings_get_boolean (settings, KEY_IS_ABSOLUTE));

	/* Ignore touch devices as they do not share the same range of values for area */
	if (type != WACOM_TYPE_TOUCH) {
		if (gsd_wacom_device_is_screen_tablet (device) == FALSE)
			set_keep_aspect (device, g_settings_get_boolean (settings, KEY_KEEP_ASPECT));
		set_area (device, g_settings_get_value (settings, KEY_AREA));
	}

        /* only pen and eraser have pressure threshold and curve settings */
        if (type == WACOM_TYPE_STYLUS ||
	    type == WACOM_TYPE_ERASER) {
		apply_stylus_settings (device);
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
		if (type == WACOM_TYPE_PAD)
			update_pad_leds (device);
                else
                        set_rotation (device, g_settings_get_enum (settings, key));
	} else if (g_str_equal (key, KEY_TOUCH)) {
	        if (type == WACOM_TYPE_TOUCH)
			set_touch (device, g_settings_get_boolean (settings, key));
	} else if (g_str_equal (key, KEY_IS_ABSOLUTE)) {
		if (type != WACOM_TYPE_CURSOR &&
		    type != WACOM_TYPE_PAD &&
		    type != WACOM_TYPE_TOUCH)
			set_absolute (device, g_settings_get_boolean (settings, key));
	} else if (g_str_equal (key, KEY_AREA)) {
		if (type != WACOM_TYPE_CURSOR &&
		    type != WACOM_TYPE_PAD &&
		    type != WACOM_TYPE_TOUCH)
			set_area (device, g_settings_get_value (settings, key));
	} else if (g_str_equal (key, KEY_KEEP_ASPECT)) {
		if (type != WACOM_TYPE_CURSOR &&
		    type != WACOM_TYPE_PAD &&
		    type != WACOM_TYPE_TOUCH &&
		    !gsd_wacom_device_is_screen_tablet (device))
			set_keep_aspect (device, g_settings_get_boolean (settings, key));
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
last_stylus_changed (GsdWacomDevice  *device,
		     GParamSpec      *pspec,
		     GsdWacomManager *manager)
{
	g_debug ("Stylus for device '%s' changed, applying settings",
		 gsd_wacom_device_get_name (device));
	apply_stylus_settings (device);
}

static void
osd_window_destroy (GsdWacomManager *manager)
{
	g_return_if_fail (manager != NULL);

        g_clear_pointer (&manager->priv->osd_window, gtk_widget_destroy);
}

static gboolean
osd_window_on_focus_out_event (GtkWidget *widget,
                               GdkEvent  *event,
                               GsdWacomManager *manager)
{
	GsdWacomOSDWindow *osd_window;

        osd_window = GSD_WACOM_OSD_WINDOW (widget);

	/* If the OSD window loses focus, hide it unless it is in edition mode */
	if (gsd_wacom_osd_window_get_edition_mode (osd_window))
		return FALSE;

	osd_window_destroy (manager);

	return FALSE;
}

static gboolean
osd_window_toggle_visibility (GsdWacomManager *manager,
                              GsdWacomDevice  *device)
{
	GtkWidget *widget;
        const gchar *layout_path;

	if (manager->priv->osd_window) {
		osd_window_destroy (manager);
		return FALSE;
	}

        layout_path = gsd_wacom_device_get_layout_path (device);
        if (layout_path == NULL) {
                g_warning ("Cannot display the on-screen help window as the tablet "
                           "definition for %s is missing the layout\n"
                           "Please consider contributing the layout for your "
                           "tablet to libwacom at linuxwacom-devel@lists.sourceforge.net\n",
                           gsd_wacom_device_get_name (device));
		return FALSE;
        }

        if (g_file_test (layout_path, G_FILE_TEST_EXISTS) == FALSE) {
                g_warning ("Cannot display the on-screen help window as the "
                           "layout file %s cannot be found on disk\n"
                           "Please check your libwacom installation\n",
                           layout_path);
		return FALSE;
        }

	widget = gsd_wacom_osd_window_new (device, NULL);

	g_signal_connect (widget, "focus-out-event",
			  G_CALLBACK(osd_window_on_focus_out_event), manager);
	g_object_add_weak_pointer (G_OBJECT (widget), (gpointer *) &manager->priv->osd_window);

	gtk_window_present (GTK_WINDOW(widget));
	manager->priv->osd_window = widget;

	return TRUE;
}

static gboolean
osd_window_update_viewable (GsdWacomManager      *manager,
                            GsdWacomTabletButton *button,
                            GtkDirectionType      dir,
                            XIEvent              *xiev)
{
	if (manager->priv->osd_window == NULL)
		return FALSE;

	gsd_wacom_osd_window_set_active (GSD_WACOM_OSD_WINDOW (manager->priv->osd_window),
	                                 button,
	                                 dir,
	                                 xiev->evtype == XI_ButtonPress);

	return TRUE;
}

static void
notify_unknown_device (GsdWacomManager *manager, const gchar *device_name)
{
        gchar *msg_body;
        NotifyNotification *notification;

        msg_body = g_strdup_printf (_("The \"%s\" tablet may not work as expected."), device_name);
        notification = notify_notification_new (_("Unknown Tablet Connected"), msg_body,
                                                "input-tablet");
        notify_notification_set_timeout (notification, UNKNOWN_DEVICE_NOTIFICATION_TIMEOUT);
        notify_notification_set_urgency (notification, NOTIFY_URGENCY_NORMAL);
        notify_notification_set_app_name (notification, _("Wacom Settings"));
        notify_notification_show (notification, NULL);

        g_signal_connect (notification, "closed",
                          G_CALLBACK (g_object_unref), NULL);

        g_free (msg_body);
}

static void
gsd_wacom_manager_add_gdk_device (GsdWacomManager *manager,
                                  GdkDevice       *gdk_device)
{
	GsdWacomDevice *device;
	GSettings *settings;
	const gchar *device_name;
	GsdWacomDeviceType type;

	device = gsd_wacom_device_new (gdk_device);
	device_name = gsd_wacom_device_get_name (device);
	type = gsd_wacom_device_get_device_type (device);

        if (gsd_wacom_device_is_fallback (device) &&
            type != WACOM_TYPE_TOUCH && device_name != NULL) {
                GHashTable *warned_devices;

                warned_devices = manager->priv->warned_devices;

                if (!g_hash_table_contains (warned_devices, device_name)) {
                        g_warning ("No definition for  '%s' found in the tablet database. Using a fallback one.",
                                   device_name);
                        g_hash_table_insert (warned_devices, g_strdup (device_name), NULL);
                        notify_unknown_device (manager, device_name);
                }
        }

	if (type == WACOM_TYPE_INVALID) {
		g_object_unref (device);
		return;
	}
	g_debug ("Adding device '%s' (type: '%s') to known devices list",
		 gsd_wacom_device_get_tool_name (device),
		 gsd_wacom_device_type_to_string (type));
	g_hash_table_insert (manager->priv->devices, (gpointer) gdk_device, device);

	settings = gsd_wacom_device_get_settings (device);
	g_signal_connect (G_OBJECT (settings), "changed",
			  G_CALLBACK (wacom_settings_changed), device);

	if (type == WACOM_TYPE_STYLUS || type == WACOM_TYPE_ERASER) {
		GList *styli, *l;

		styli = gsd_wacom_device_list_styli (device);

		for (l = styli ; l ; l = l->next) {
			settings = gsd_wacom_stylus_get_settings (l->data);
			g_signal_connect (G_OBJECT (settings), "changed",
					  G_CALLBACK (stylus_settings_changed), l->data);
		}

		g_list_free (styli);

		g_signal_connect (G_OBJECT (device), "notify::last-stylus",
				  G_CALLBACK (last_stylus_changed), manager);
	}

        set_wacom_settings (manager, device);
}

static void
device_added_cb (GsdDeviceManager *device_manager,
                 GsdDevice        *gsd_device,
                 GsdWacomManager  *manager)
{
	GdkDevice **devices;
	GsdDeviceType device_type;
	guint i, n_gdk_devices;

	device_type = gsd_device_get_device_type (gsd_device);

	if (device_type & GSD_DEVICE_TYPE_TABLET) {
		gsd_device_mapper_add_input (manager->priv->device_mapper,
					     gsd_device);
	}

	if (gnome_settings_is_wayland ())
	    return;

	devices = gsd_x11_device_manager_get_gdk_devices (GSD_X11_DEVICE_MANAGER (device_manager),
							  gsd_device, &n_gdk_devices);

	for (i = 0; i < n_gdk_devices; i++)
		gsd_wacom_manager_add_gdk_device (manager, devices[i]);

	g_free (devices);
}

static void
device_changed_cb (GsdDeviceManager *device_manager,
		   GsdDevice	    *gsd_device,
		   GsdWacomManager  *manager)
{
	GdkDevice **devices;
	guint i, n_gdk_devices;

	if (gnome_settings_is_wayland ())
		return;

	devices = gsd_x11_device_manager_get_gdk_devices (GSD_X11_DEVICE_MANAGER (device_manager),
							  gsd_device, &n_gdk_devices);

	for (i = 0; i < n_gdk_devices; i++) {
		if (!g_hash_table_lookup (manager->priv->devices, devices[i]))
			gsd_wacom_manager_add_gdk_device (manager, devices[i]);
	}

	g_free (devices);
}

static void
gsd_wacom_manager_remove_gdk_device (GsdWacomManager *manager,
                                     GdkDevice       *gdk_device)
{
	GsdWacomDevice *device;
	GSettings *settings;
	GsdWacomDeviceType type;

	g_debug ("Removing device '%s' from known devices list",
		 gdk_device_get_name (gdk_device));

	device = g_hash_table_lookup (manager->priv->devices, gdk_device);
	if (!device)
		return;

	type = gsd_wacom_device_get_device_type (device);
	settings = gsd_wacom_device_get_settings (device);

	g_signal_handlers_disconnect_by_data (G_OBJECT (settings), device);

	if (type == WACOM_TYPE_STYLUS || type == WACOM_TYPE_ERASER) {
		GList *styli, *l;

		styli = gsd_wacom_device_list_styli (device);

		for (l = styli ; l ; l = l->next) {
			settings = gsd_wacom_stylus_get_settings (l->data);
			g_signal_handlers_disconnect_by_data (G_OBJECT (settings), l->data);
		}

		g_list_free (styli);
	}

	g_hash_table_remove (manager->priv->devices, gdk_device);

	/* Enable this chunk of code if you want to valgrind
	 * test-wacom. It will exit when there are no Wacom devices left */
#if 0
	if (g_hash_table_size (manager->priv->devices) == 0)
		gtk_main_quit ();
#endif
}

static void
device_removed_cb (GsdDeviceManager *device_manager,
                   GsdDevice        *gsd_device,
                   GsdWacomManager  *manager)
{
	GdkDevice **devices;
	guint i, n_gdk_devices;

	gsd_device_mapper_remove_input (manager->priv->device_mapper,
					gsd_device);

	if (gnome_settings_is_wayland ())
	    return;

	devices = gsd_x11_device_manager_get_gdk_devices (GSD_X11_DEVICE_MANAGER (device_manager),
							  gsd_device, &n_gdk_devices);

	for (i = 0; i < n_gdk_devices; i++)
		gsd_wacom_manager_remove_gdk_device (manager, devices[i]);

	g_free (devices);
}

static GsdWacomDevice *
device_id_to_device (GsdWacomManager *manager,
		     int              deviceid)
{
	GList *devices, *l;
	GsdWacomDevice *ret;

	ret = NULL;
	devices = g_hash_table_get_keys (manager->priv->devices);

	for (l = devices; l != NULL; l = l->next) {
		GdkDevice *device = l->data;
		int id;

		g_object_get (device, "device-id", &id, NULL);
		if (id == deviceid) {
			ret = g_hash_table_lookup (manager->priv->devices, device);
			break;
		}
	}

	g_list_free (devices);
	return ret;
}

struct {
	guint mask;
	KeySym keysym;
} mods_keysyms[] = {
	{ GDK_MOD1_MASK, XK_Alt_L },
	{ GDK_SHIFT_MASK, XK_Shift_L },
	{ GDK_CONTROL_MASK, XK_Control_L },
};

static void
send_modifiers (Display *display,
		guint mask,
		gboolean is_press)
{
	guint i;

	if (mask == 0)
		return;

	for (i = 0; i < G_N_ELEMENTS(mods_keysyms); i++) {
		if (mask & mods_keysyms[i].mask) {
			guint keycode;

			keycode = XKeysymToKeycode (display, mods_keysyms[i].keysym);
			XTestFakeKeyEvent (display, keycode,
					   is_press ? True : False, 0);
		}
	}
}

static char *
get_elevator_shortcut_string (GSettings        *settings,
			      GtkDirectionType  dir)
{
	char **strv, *str;

	strv = g_settings_get_strv (settings, KEY_CUSTOM_ELEVATOR_ACTION);
	if (strv == NULL)
		return NULL;

	if (g_strv_length (strv) >= 1 && dir == GTK_DIR_UP)
		str = g_strdup (strv[0]);
	else if (g_strv_length (strv) >= 2 && dir == GTK_DIR_DOWN)
		str = g_strdup (strv[1]);
	else
		str = NULL;

	g_strfreev (strv);

	return str;
}

static void
generate_key (GsdWacomTabletButton *wbutton,
	      int                   group,
	      Display              *display,
	      GtkDirectionType      dir,
	      gboolean              is_press)
{
	char                 *str;
	guint                 keyval;
	guint                *keycodes;
	guint                 keycode;
	guint                 mods;
	GdkKeymapKey         *keys;
	int                   n_keys;
	guint                 i;

	if (wbutton->type == WACOM_TABLET_BUTTON_TYPE_STRIP ||
	    wbutton->type == WACOM_TABLET_BUTTON_TYPE_RING)
		str = get_elevator_shortcut_string (wbutton->settings, dir);
	else
		str = g_settings_get_string (wbutton->settings, KEY_CUSTOM_ACTION);

	if (str == NULL)
		return;

	gtk_accelerator_parse_with_keycode (str, &keyval, &keycodes, &mods);
	if (keycodes == NULL) {
		g_warning ("Failed to find a keycode for shortcut '%s'", str);
		g_free (str);
		return;
	}
	g_free (keycodes);

	/* Now look for our own keycode, in the group as us */
	if (!gdk_keymap_get_entries_for_keyval (gdk_keymap_get_default (), keyval, &keys, &n_keys)) {
		g_warning ("Failed to find a keycode for keyval '%s' (0x%x)", gdk_keyval_name (keyval), keyval);
		g_free (str);
		return;
	}

	keycode = 0;
	for (i = 0; i < n_keys; i++) {
		if (keys[i].group != group)
			continue;
		if (keys[i].level > 0)
			continue;
		keycode = keys[i].keycode;
	}
	/* Couldn't find it in the current group? Look in group 0 */
	if (keycode == 0) {
		for (i = 0; i < n_keys; i++) {
			if (keys[i].group > 0)
				continue;
			keycode = keys[i].keycode;
		}
	}
	g_free (keys);

	if (keycode == 0) {
		g_warning ("Not emitting '%s' (keyval: %d, keycode: %d mods: 0x%x), invalid keycode",
			   str, keyval, keycode, mods);
		g_free (str);
		return;
	} else {
		g_debug ("Emitting '%s' (keyval: %d, keycode: %d mods: 0x%x)",
			 str, keyval, keycode, mods);
	}

	/* And send out the keys! */
	gdk_error_trap_push ();
	if (is_press)
		send_modifiers (display, mods, TRUE);
	XTestFakeKeyEvent (display, keycode,
			   is_press ? True : False, 0);
	if (is_press == FALSE)
		send_modifiers (display, mods, FALSE);
	if (gdk_error_trap_pop ())
		g_warning ("Failed to generate fake key event '%s'", str);

	g_free (str);
}

static void
switch_monitor (GsdWacomManager *manager,
                GsdWacomDevice *device)
{
	gint current_monitor, n_monitors;
        GdkDevice *gdk_device;
        GsdDevice *gsd_device;

	/* We dont; do that for screen tablets, sorry... */
	if (gsd_wacom_device_is_screen_tablet (device))
		return;

	n_monitors = gdk_screen_get_n_monitors (gdk_screen_get_default ());

	/* There's no point in switching if there just one monitor */
	if (n_monitors < 2)
		return;

        gdk_device = gsd_wacom_device_get_gdk_device (device);
        gsd_device = gsd_x11_device_manager_lookup_gdk_device (GSD_X11_DEVICE_MANAGER (gsd_device_manager_get ()),
                                                               gdk_device);
        current_monitor =
                gsd_device_mapper_get_device_monitor (manager->priv->device_mapper,
                                                      gsd_device);

	/* Select next monitor */
	current_monitor++;

	if (current_monitor >= n_monitors)
		current_monitor = 0;

        gsd_device_mapper_set_device_monitor (manager->priv->device_mapper,
                                              gsd_device, current_monitor);
}

static void
notify_osd_for_device (GsdWacomManager *manager,
                       GsdWacomDevice  *device)
{
        GdkDevice *gdk_device;
        GsdDevice *gsd_device;
        GdkScreen *screen;
        gint monitor_num;

        gdk_device = gsd_wacom_device_get_gdk_device (device);
        gsd_device = gsd_x11_device_manager_lookup_gdk_device (GSD_X11_DEVICE_MANAGER (gsd_device_manager_get ()),
                                                               gdk_device);
        monitor_num = gsd_device_mapper_get_device_monitor (manager->priv->device_mapper,
                                                            gsd_device);

        if (monitor_num == GSD_WACOM_SET_ALL_MONITORS)
                return;

        screen = gdk_screen_get_default ();

        if (gdk_screen_get_n_monitors (screen) == 1)
                return;

        if (manager->priv->shell_proxy == NULL)
                manager->priv->shell_proxy = gnome_settings_bus_get_shell_proxy ();

        shell_show_osd (manager->priv->shell_proxy,
                        "input-tablet-symbolic",
                        gsd_wacom_device_get_name (device), -1,
                        monitor_num);
}

static const char*
get_direction_name (GsdWacomTabletButtonType type,
                    GtkDirectionType         dir)
{
        if (type == WACOM_TABLET_BUTTON_TYPE_RING)
                return (dir == GTK_DIR_UP ? " 'CCW'" : " 'CW'");

        if (type == WACOM_TABLET_BUTTON_TYPE_STRIP)
                return (dir == GTK_DIR_UP ? " 'up'" : " 'down'");

        return "";
}

static GdkFilterReturn
filter_button_events (XEvent          *xevent,
                      GdkEvent        *event,
                      GsdWacomManager *manager)
{
	XIEvent             *xiev;
	XIDeviceEvent       *xev;
	XGenericEventCookie *cookie;
	guint                deviceid;
	GsdWacomDevice      *device;
	int                  button;
	GsdWacomTabletButton *wbutton;
	GtkDirectionType      dir;
	gboolean emulate;

        /* verify we have a key event */
	if (xevent->type != GenericEvent)
		return GDK_FILTER_CONTINUE;
	cookie = &xevent->xcookie;
	if (cookie->extension != manager->priv->opcode)
		return GDK_FILTER_CONTINUE;

	xiev = (XIEvent *) xevent->xcookie.data;

	if (xiev->evtype != XI_ButtonRelease &&
	    xiev->evtype != XI_ButtonPress)
		return GDK_FILTER_CONTINUE;

	xev = (XIDeviceEvent *) xiev;

	deviceid = xev->sourceid;
	device = device_id_to_device (manager, deviceid);
	if (gsd_wacom_device_get_device_type (device) != WACOM_TYPE_PAD)
		return GDK_FILTER_CONTINUE;

	if ((manager->priv->osd_window != NULL) &&
	    (device != gsd_wacom_osd_window_get_device (GSD_WACOM_OSD_WINDOW(manager->priv->osd_window))))
		/* This is a button event from another device while showing OSD window */
		osd_window_destroy (manager);

	button = xev->detail;

	wbutton = gsd_wacom_device_get_button (device, button, &dir);
	if (wbutton == NULL) {
		g_warning ("Could not find matching button for '%d' on '%s'",
			   button, gsd_wacom_device_get_name (device));
		return GDK_FILTER_CONTINUE;
	}

	g_debug ("Received event button %s '%s'%s ('%d') on device '%s' ('%d')",
		 xiev->evtype == XI_ButtonPress ? "press" : "release",
		 wbutton->id,
		 get_direction_name (wbutton->type, dir),
		 button,
		 gsd_wacom_device_get_name (device),
		 deviceid);

	if (wbutton->type == WACOM_TABLET_BUTTON_TYPE_HARDCODED) {
		int new_mode;

		/* We switch modes on key press */
		if (xiev->evtype == XI_ButtonRelease) {
			osd_window_update_viewable (manager, wbutton, dir, xiev);
			return GDK_FILTER_REMOVE;
                }
		new_mode = gsd_wacom_device_set_next_mode (device, wbutton);
                if (manager->priv->osd_window != NULL) {
			gsd_wacom_osd_window_set_mode (GSD_WACOM_OSD_WINDOW(manager->priv->osd_window), wbutton->group_id, new_mode);
			osd_window_update_viewable (manager, wbutton, dir, xiev);
                }
		set_led (device, wbutton, new_mode);
		return GDK_FILTER_REMOVE;
	}

	if (manager->priv->osd_window != NULL) {
		GsdWacomDevice *osd_window_device;
		gboolean edition_mode;

		g_object_get (manager->priv->osd_window,
                              "wacom-device", &osd_window_device,
                              "edition-mode", &edition_mode, NULL);

		if (osd_window_device && device == osd_window_device && edition_mode) {
			osd_window_update_viewable (manager, wbutton, dir, xiev);
			g_object_unref (osd_window_device);

			return GDK_FILTER_REMOVE;
		}

		g_object_unref (osd_window_device);
	}

	/* Update OSD window if shown */
	emulate = osd_window_update_viewable (manager, wbutton, dir, xiev);

	/* Nothing to do */
	if (g_settings_get_enum (wbutton->settings, KEY_ACTION_TYPE) == GSD_WACOM_ACTION_TYPE_NONE)
		return GDK_FILTER_REMOVE;

	/* Show OSD window when requested */
	if (g_settings_get_enum (wbutton->settings, KEY_ACTION_TYPE) == GSD_WACOM_ACTION_TYPE_HELP) {
		if (xiev->evtype == XI_ButtonRelease)
			osd_window_toggle_visibility (manager, device);
		return GDK_FILTER_REMOVE;
	}

	if (emulate)
		return GDK_FILTER_REMOVE;

	/* Switch monitor */
	if (g_settings_get_enum (wbutton->settings, KEY_ACTION_TYPE) == GSD_WACOM_ACTION_TYPE_SWITCH_MONITOR) {
		if (xiev->evtype == XI_ButtonRelease) {
			switch_monitor (manager, device);
			notify_osd_for_device (manager, device);
		}
		return GDK_FILTER_REMOVE;
	}

	/* Send a key combination out */
	generate_key (wbutton, xev->group.effective, xev->display, dir, xiev->evtype == XI_ButtonPress ? True : False);

	return GDK_FILTER_REMOVE;
}

static void
set_devicepresence_handler (GsdWacomManager *manager)
{
        GsdDeviceManager *device_manager;

        device_manager = gsd_device_manager_get ();
        manager->priv->device_added_id = g_signal_connect (G_OBJECT (device_manager), "device-added",
                                                           G_CALLBACK (device_added_cb), manager);
        manager->priv->device_changed_id = g_signal_connect (G_OBJECT (device_manager), "device-changed",
                                                             G_CALLBACK (device_changed_cb), manager);
        manager->priv->device_removed_id = g_signal_connect (G_OBJECT (device_manager), "device-removed",
                                                             G_CALLBACK (device_removed_cb), manager);
        manager->priv->device_manager = device_manager;
}

static void
gsd_wacom_manager_init (GsdWacomManager *manager)
{
        manager->priv = GSD_WACOM_MANAGER_GET_PRIVATE (manager);
}

static void
device_mapping_changed (GsdDeviceMapper *mapper,
                        GsdDevice       *gsd_device,
                        GsdWacomManager *manager)
{
	guint i, n_gdk_devices;
	GdkDevice **devices;

	if (gnome_settings_is_wayland ())
		return;

	devices = gsd_x11_device_manager_get_gdk_devices (GSD_X11_DEVICE_MANAGER (manager->priv->device_manager),
							  gsd_device, &n_gdk_devices);

	for (i = 0; i < n_gdk_devices; i++) {
                GsdWacomDevice *wacom_device;
                GsdWacomDeviceType type;
                GSettings *settings;

                wacom_device = g_hash_table_lookup (manager->priv->devices, devices[i]);

                if (!wacom_device)
                        continue;

                settings = gsd_wacom_device_get_settings (wacom_device);
                type = gsd_wacom_device_get_device_type (wacom_device);

                if (type != WACOM_TYPE_TOUCH && type != WACOM_TYPE_PAD)
                        set_keep_aspect (wacom_device, g_settings_get_boolean (settings, KEY_KEEP_ASPECT));
        }

        g_free (devices);
}

static gboolean
gsd_wacom_manager_idle_cb (GsdWacomManager *manager)
{
	GList *devices, *l;

        gnome_settings_profile_start (NULL);

        manager->priv->device_mapper = gsd_device_mapper_get ();

        manager->priv->mapping_changed_id =
                g_signal_connect (manager->priv->device_mapper, "device-changed",
                                  G_CALLBACK (device_mapping_changed), manager);

        manager->priv->warned_devices = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

        manager->priv->devices = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);

        set_devicepresence_handler (manager);

        devices = gsd_device_manager_list_devices (manager->priv->device_manager,
                                                   GSD_DEVICE_TYPE_TABLET);
        for (l = devices; l ; l = l->next)
		device_added_cb (manager->priv->device_manager, l->data, manager);
        g_list_free (devices);

        if (!gnome_settings_is_wayland ()) {
                /* Start filtering the button events */
                gdk_window_add_filter (gdk_screen_get_root_window (manager->priv->screen),
                                       (GdkFilterFunc) filter_button_events,
                                       manager);
        }

        gnome_settings_profile_end (NULL);

        manager->priv->start_idle_id = 0;

        return FALSE;
}

/*
 * The monitors-changed signal is emitted when the number, size or
 * position of the monitors attached to the screen change.
 */
static void
on_screen_changed_cb (GnomeRRScreen *rr_screen,
		      GsdWacomManager *manager)
{
	GList *devices, *l;

        /*
         * A ::changed signal may be received at startup before
         * the devices get added, in this case simply ignore the
         * notification
         */
        if (manager->priv->devices == NULL)
                return;

        g_debug ("Screen configuration changed");
	devices = g_hash_table_get_values (manager->priv->devices);
	for (l = devices; l != NULL; l = l->next) {
		GsdWacomDevice *device = l->data;
		GsdWacomDeviceType type;
		GSettings *settings;

		type = gsd_wacom_device_get_device_type (device);
		if (type == WACOM_TYPE_CURSOR || type == WACOM_TYPE_PAD)
			continue;

		settings = gsd_wacom_device_get_settings (device);
		/* Ignore touch devices as they do not share the same range of values for area */
		if (type != WACOM_TYPE_TOUCH) {
			if (gsd_wacom_device_is_screen_tablet (device) == FALSE) {
				set_keep_aspect (device, g_settings_get_boolean (settings, KEY_KEEP_ASPECT));
			}

			set_area (device, g_settings_get_value (settings, KEY_AREA));
		}
	}
	g_list_free (devices);
}

static void
on_rr_screen_acquired (GObject      *object,
                       GAsyncResult *result,
                       gpointer      user_data)
{
        GsdWacomManager *manager = user_data;
        GError *error = NULL;

        manager->priv->rr_screen = gnome_rr_screen_new_finish (result, &error);
        if (manager->priv->rr_screen == NULL) {
                g_warning ("Failed to create GnomeRRScreen: %s", error->message);
                g_error_free (error);
                return;
        }

        g_signal_connect (manager->priv->rr_screen,
                          "changed",
                          G_CALLBACK (on_screen_changed_cb),
                          manager);
}

static void
init_screen (GsdWacomManager *manager)
{
        GdkScreen *screen;

        screen = gdk_screen_get_default ();
        if (screen == NULL) {
                return;
        }
        manager->priv->screen = screen;

        /*
         * We keep GnomeRRScreen to monitor changes such as rotation
         * which are not reported by Gdk's "monitors-changed" callback
         */
        gnome_rr_screen_new_async (screen, on_rr_screen_acquired, manager);
}

static void
on_bus_gotten (GObject		   *source_object,
	       GAsyncResult	   *res,
	       GsdWacomManager	   *manager)
{
	GDBusConnection	       *connection;
	GError		       *error = NULL;
	GsdWacomManagerPrivate *priv;

	priv = manager->priv;

	connection = g_bus_get_finish (res, &error);

	if (connection == NULL) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning ("Couldn't get session bus: %s", error->message);
		g_error_free (error);
		return;
	}

	priv->dbus_connection = connection;
	priv->dbus_register_object_id = g_dbus_connection_register_object (connection,
									   GSD_WACOM_DBUS_PATH,
									   priv->introspection_data->interfaces[0],
									   &interface_vtable,
									   manager,
									   NULL,
									   &error);

	if (priv->dbus_register_object_id == 0) {
		g_warning ("Error registering object: %s", error->message);
		g_error_free (error);
		return;
	}

        manager->priv->name_id = g_bus_own_name_on_connection (connection,
                                                               GSD_WACOM_DBUS_NAME,
                                                               G_BUS_NAME_OWNER_FLAGS_NONE,
                                                               NULL,
                                                               NULL,
                                                               NULL,
                                                               NULL);
}

static void
register_manager (GsdWacomManager *manager)
{
        manager->priv->introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
        manager->priv->dbus_cancellable = g_cancellable_new ();
        g_assert (manager->priv->introspection_data != NULL);

        g_bus_get (G_BUS_TYPE_SESSION,
                   manager->priv->dbus_cancellable,
                   (GAsyncReadyCallback) on_bus_gotten,
                   manager);
}

gboolean
gsd_wacom_manager_start (GsdWacomManager *manager,
                         GError         **error)
{
        gnome_settings_profile_start (NULL);

        if (supports_xinput2_devices (&manager->priv->opcode) == FALSE) {
                g_debug ("No Xinput2 support, disabling plugin");
                return TRUE;
        }

        if (supports_xtest () == FALSE) {
                g_debug ("No XTest extension support, disabling plugin");
                return TRUE;
        }

        init_screen (manager);
        register_manager (manager_object);

        manager->priv->start_idle_id = g_idle_add ((GSourceFunc) gsd_wacom_manager_idle_cb, manager);
        g_source_set_name_by_id (manager->priv->start_idle_id, "[gnome-settings-daemon] gsd_wacom_manager_idle_cb");

        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_wacom_manager_stop (GsdWacomManager *manager)
{
        GsdWacomManagerPrivate *p = manager->priv;
        GsdWacomDeviceType type;
        GsdWacomDevice *device;
        GHashTableIter iter;

        g_debug ("Stopping wacom manager");

        if (manager->priv->name_id != 0) {
                g_bus_unown_name (manager->priv->name_id);
                manager->priv->name_id = 0;
        }

        if (p->dbus_register_object_id) {
                g_dbus_connection_unregister_object (p->dbus_connection,
                                                     p->dbus_register_object_id);
                p->dbus_register_object_id = 0;
        }

        if (p->device_manager != NULL) {
                g_signal_handler_disconnect (p->device_manager, p->device_added_id);
                g_signal_handler_disconnect (p->device_manager, p->device_changed_id);
                g_signal_handler_disconnect (p->device_manager, p->device_removed_id);
                p->device_manager = NULL;
        }

        if (!gnome_settings_is_wayland ()) {
                g_hash_table_iter_init (&iter, manager->priv->devices);

                while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &device)) {
                        type = gsd_wacom_device_get_device_type (device);
                        if (type == WACOM_TYPE_PAD) {
                                GdkDevice *gdk_device;

                                gdk_device = gsd_wacom_device_get_gdk_device (device);
                                grab_button (gdk_x11_device_get_id (gdk_device),
                                             FALSE, manager->priv->screen);
                        }
                }

                gdk_window_remove_filter (gdk_screen_get_root_window (p->screen),
                                          (GdkFilterFunc) filter_button_events,
                                          manager);
        }

        g_signal_handlers_disconnect_by_func (p->rr_screen, on_screen_changed_cb, manager);

        g_clear_pointer (&p->osd_window, gtk_widget_destroy);
}

static void
gsd_wacom_manager_finalize (GObject *object)
{
        GsdWacomManager *wacom_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_WACOM_MANAGER (object));

        wacom_manager = GSD_WACOM_MANAGER (object);

        g_return_if_fail (wacom_manager->priv != NULL);

        gsd_wacom_manager_stop (wacom_manager);

        if (wacom_manager->priv->mapping_changed_id) {
                g_signal_handler_disconnect (wacom_manager->priv->device_mapper,
                                             wacom_manager->priv->mapping_changed_id);
        }

        if (wacom_manager->priv->warned_devices) {
                g_hash_table_destroy (wacom_manager->priv->warned_devices);
                wacom_manager->priv->warned_devices = NULL;
        }

        if (wacom_manager->priv->devices) {
                g_hash_table_destroy (wacom_manager->priv->devices);
                wacom_manager->priv->devices = NULL;
        }

	if (wacom_manager->priv->rr_screen != NULL) {
		g_clear_object (&wacom_manager->priv->rr_screen);
		wacom_manager->priv->rr_screen = NULL;
	}

        if (wacom_manager->priv->start_idle_id != 0)
                g_source_remove (wacom_manager->priv->start_idle_id);

        g_clear_object (&wacom_manager->priv->shell_proxy);

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
