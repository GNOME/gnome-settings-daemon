/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Bastien Nocera <hadess@hadess.net>
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

#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#include <sys/types.h>
#include <X11/Xatom.h>
#include <X11/extensions/XInput2.h>

#include "gsd-input-helper.h"

#define INPUT_DEVICES_SCHEMA "org.gnome.settings-daemon.peripherals.input-devices"
#define KEY_HOTPLUG_COMMAND  "hotplug-command"

typedef gboolean (* InfoIdentifyFunc) (XDeviceInfo *device_info);
typedef gboolean (* DeviceIdentifyFunc) (XDevice *xdevice);

gboolean
device_set_property (XDevice        *xdevice,
                     const char     *device_name,
                     PropertyHelper *property)
{
        int rc;
        Atom prop;
        Atom realtype;
        int realformat;
        unsigned long nitems, bytes_after;
        unsigned char *data;

        prop = XInternAtom (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                            property->name, False);
        if (!prop)
                return FALSE;

        gdk_error_trap_push ();

        rc = XGetDeviceProperty (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                                 xdevice, prop, 0, property->nitems, False,
                                 XA_INTEGER, &realtype, &realformat, &nitems,
                                 &bytes_after, &data);

        if (rc == Success && realtype == XA_INTEGER &&
            realformat == property->format && nitems >= property->nitems) {
                int i;
                for (i = 0; i < nitems; i++) {
                        switch (property->format) {
                                case 8:
                                        data[i] = property->data.c[i];
                                        break;
                                case 32:
                                        ((long*)data)[i] = property->data.i[i];
                                        break;
                        }
                }

                XChangeDeviceProperty (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                                       xdevice, prop, XA_INTEGER, realformat,
                                       PropModeReplace, data, nitems);
        }

        if (gdk_error_trap_pop ()) {
                g_warning ("Error in setting \"%s\" for \"%s\"", property->name, device_name);
                return FALSE;
        }

        return TRUE;
}

gboolean
supports_xinput_devices (void)
{
        gint op_code, event, error;

        return XQueryExtension (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                                "XInputExtension",
                                &op_code,
                                &event,
                                &error);
}

gboolean
supports_xinput2_devices (void)
{
        int major, minor;

        if (supports_xinput_devices () == FALSE)
                return FALSE;

        gdk_error_trap_push ();

        major = XI_2_Major;
        minor = XI_2_Minor;

        if (XIQueryVersion (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), &major, &minor) != Success) {
                gdk_error_trap_pop_ignored ();
                return FALSE;
        }
        gdk_error_trap_pop_ignored ();

        if ((major * 1000 + minor) < (XI_2_Major * 1000 + XI_2_Minor))
                return FALSE;

        return TRUE;
}

gboolean
device_is_touchpad (XDevice *xdevice)
{
        Atom realtype, prop;
        int realformat;
        unsigned long nitems, bytes_after;
        unsigned char *data;

        /* we don't check on the type being XI_TOUCHPAD here,
         * but having a "Synaptics Off" property should be enough */

        prop = XInternAtom (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), "Synaptics Off", False);
        if (!prop)
                return FALSE;

        gdk_error_trap_push ();
        if ((XGetDeviceProperty (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), xdevice, prop, 0, 1, False,
                                XA_INTEGER, &realtype, &realformat, &nitems,
                                &bytes_after, &data) == Success) && (realtype != None)) {
                gdk_error_trap_pop_ignored ();
                XFree (data);
                return TRUE;
        }
        gdk_error_trap_pop_ignored ();

        return FALSE;
}

gboolean
device_info_is_touchpad (XDeviceInfo *device_info)
{
        return (device_info->type == XInternAtom (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), XI_TOUCHPAD, False));
}

gboolean
device_info_is_touchscreen (XDeviceInfo *device_info)
{
        return (device_info->type == XInternAtom (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), XI_TOUCHSCREEN, False));
}

static gboolean
device_type_is_present (InfoIdentifyFunc info_func,
                        DeviceIdentifyFunc device_func)
{
        XDeviceInfo *device_info;
        gint n_devices;
        guint i;
        gboolean retval;

        if (supports_xinput_devices () == FALSE)
                return TRUE;

        retval = FALSE;

        device_info = XListInputDevices (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), &n_devices);
        if (device_info == NULL)
                return FALSE;

        for (i = 0; i < n_devices; i++) {
                XDevice *device;

                /* Check with the device info first */
                retval = (info_func) (&device_info[i]);
                if (retval == FALSE)
                        continue;

                /* If we only have an info func, we're done checking */
                if (device_func == NULL)
                        break;

                gdk_error_trap_push ();
                device = XOpenDevice (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), device_info[i].id);
                if (gdk_error_trap_pop () || (device == NULL))
                        continue;

                retval = (device_func) (device);
                if (retval) {
                        XCloseDevice (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), device);
                        break;
                }

                XCloseDevice (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), device);
        }
        XFreeDeviceList (device_info);

        return retval;
}

gboolean
touchscreen_is_present (void)
{
        return device_type_is_present (device_info_is_touchscreen,
                                       NULL);
}

gboolean
touchpad_is_present (void)
{
        return device_type_is_present (device_info_is_touchpad,
                                       device_is_touchpad);
}

static char *
get_device_node (int deviceid)
{
        Atom           prop;
        Atom           act_type;
        int            act_format;
        unsigned long  nitems, bytes_after;
        unsigned char *data;
        char          *ret;

        gdk_display_sync (gdk_display_get_default ());

        prop = XInternAtom (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), "Device Node", False);
        if (!prop)
                return NULL;

        gdk_error_trap_push ();

        if (!XIGetProperty (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                            deviceid, prop, 0, 1000, False,
                            AnyPropertyType, &act_type, &act_format,
                            &nitems, &bytes_after, &data) == Success) {
                gdk_error_trap_pop_ignored ();
                return NULL;
        }
        if (gdk_error_trap_pop ())
                goto out;

        if (nitems == 0)
                goto out;

        if (act_type != XA_STRING)
                goto out;

        /* Unknown string format */
        if (act_format != 8)
                goto out;

        ret = g_strdup ((char *) data);

        XFree (data);
        return ret;

out:
        XFree (data);
        return NULL;
}

gboolean
accelerometer_is_present (char **device_node,
                          int   *device_id)
{
        XIDeviceInfo *device_info;
        gint n_devices;
        guint i;
        gboolean retval;

        if (supports_xinput2_devices () == FALSE)
                return FALSE;

        /* Find the accelerometer device,
         * Floating, and with 3 axis */
        device_info = XIQueryDevice (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), XIAllDevices, &n_devices);

        if (device_info == NULL)
                return FALSE;

        retval = FALSE;

        for (i = 0; i < n_devices; i++) {
                XIDeviceInfo *device;
                gboolean all_vals;
                guint j;
                char *node;

                device = &device_info[i];

                /* Check for a floating device */
                if (device->use != XIFloatingSlave)
                        continue;

                /* Not 3 classes? Probably not an accelerometer */
                if (device->num_classes != 3)
                        continue;

                /* Check for a valuator class */
                all_vals = TRUE;
                for (j = 0; j < device->num_classes; j++) {
                        if (device->classes[j]->type != XIValuatorClass) {
                                all_vals = FALSE;
                                break;
                        }
                }
                if (all_vals == FALSE)
                        continue;

                /* Look for the "Device Node" property */
                node = get_device_node (device->deviceid);
                if (device_node != NULL)
                        *device_node = node;
                else
                        g_free (node);
                if (device_id != NULL)
                        *device_id = device->deviceid;

                retval = TRUE;

                break;
        }

        XIFreeDeviceInfo (device_info);

        return retval;
}

gboolean
set_device_enabled (int device_id,
                    gboolean enabled)
{
        Atom prop;
        guchar value;

        prop = XInternAtom (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), "Device Enabled", False);
        if (!prop)
                return FALSE;

        gdk_error_trap_push ();

        value = enabled;
        XIChangeProperty (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                          device_id, prop, XA_INTEGER, 8, PropModeReplace, &value, 1);

        if (gdk_error_trap_pop ())
                return FALSE;

        return TRUE;
}

static const char *
custom_command_to_string (CustomCommand command)
{
        switch (command) {
        case COMMAND_DEVICE_ADDED:
                return "added";
        case COMMAND_DEVICE_REMOVED:
                return "removed";
        case COMMAND_DEVICE_PRESENT:
                return "present";
        default:
                g_assert_not_reached ();
        }
}

/* Run a custom command on device presence events. Parameters passed into
 * the custom command are:
 * command -t [added|removed|present] -i <device ID> <device name>
 * Type 'added' and 'removed' signal 'device added' and 'device removed',
 * respectively. Type 'present' signals 'device present at
 * gnome-settings-daemon init'.
 *
 * The script is expected to run synchronously, and an exit value
 * of "1" means that no other settings will be applied to this
 * particular device.
 *
 * More options may be added in the future.
 *
 * This function returns TRUE if we should not apply any more settings
 * to the device.
 */
gboolean
run_custom_command (GdkDevice              *device,
                    CustomCommand           command)
{
        GSettings *settings;
        char *cmd;
        char *argv[5];
        int exit_status;
        gboolean rc;
        int id;

        settings = g_settings_new (INPUT_DEVICES_SCHEMA);
        cmd = g_settings_get_string (settings, KEY_HOTPLUG_COMMAND);
        g_object_unref (settings);

        if (!cmd || cmd[0] == '\0') {
                g_free (cmd);
                return FALSE;
        }

        /* Easter egg! */
        g_object_get (device, "device-id", &id, NULL);

        argv[0] = cmd;
        argv[1] = g_strdup_printf ("-t %s", custom_command_to_string (command));
        argv[2] = g_strdup_printf ("-i %d", id);
        argv[3] = g_strdup_printf ("%s", gdk_device_get_name (device));
        argv[4] = NULL;

        rc = g_spawn_sync (g_get_home_dir (), argv, NULL, G_SPAWN_SEARCH_PATH,
                           NULL, NULL, NULL, NULL, &exit_status, NULL);

        if (rc == FALSE)
                g_warning ("Couldn't execute command '%s', verify that this is a valid command.", cmd);

        g_free (argv[0]);
        g_free (argv[1]);
        g_free (argv[2]);

        return (exit_status == 0);
}
