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
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <X11/Xatom.h>

#include <X11/extensions/XInput.h>

#include "gsd-enums.h"
#include "gnome-settings-profile.h"
#include "gsd-wacom-manager.h"

/* Maximum number of buttons map-able. */
#define WACOM_MAX_BUTTONS 32
/* Device types to apply a setting to */
#define WACOM_TYPE_STYLUS       (1 << 1)
#define WACOM_TYPE_ERASER       (1 << 2)
#define WACOM_TYPE_CURSOR       (1 << 3)
#define WACOM_TYPE_PAD          (1 << 4)
#define WACOM_TYPE_ALL          WACOM_TYPE_STYLUS | WACOM_TYPE_ERASER | WACOM_TYPE_CURSOR | WACOM_TYPE_PAD

#define GSD_WACOM_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_WACOM_MANAGER, GsdWacomManagerPrivate))

/* we support two types of settings:
 * Tablet-wide settings: applied to each tool on the tablet. e.g. rotation
 * Tool-specific settings: applied to one tool only.
 */
#define SETTINGS_WACOM_DIR         "org.gnome.settings-daemon.peripherals.wacom"
#define SETTINGS_STYLUS_DIR        SETTINGS_WACOM_DIR ".stylus"
#define SETTINGS_CURSOR_DIR        SETTINGS_WACOM_DIR ".cursor"
#define SETTINGS_ERASER_DIR        SETTINGS_WACOM_DIR ".eraser"
#define SETTINGS_PAD_DIR           SETTINGS_WACOM_DIR ".pad"

#define KEY_ROTATION            "rotation"
#define KEY_TOUCH               "touch"
#define KEY_TPCBUTTON           "tablet-pc-button"
#define KEY_PRESSURECURVE       "pressurecurve"
#define KEY_IS_ABSOLUTE         "is-absolute"
#define KEY_AREA                "area"
#define KEY_BUTTONS             "buttonmapping"
#define KEY_PRESSURETHRESHOLD   "pressurethreshold"

struct GsdWacomManagerPrivate
{
	guint start_idle_id;
        GSettings *wacom_settings;
        GSettings *stylus_settings;
        GSettings *eraser_settings;
        GSettings *cursor_settings;
        GSettings *pad_settings;
        GdkDeviceManager *device_manager;
        guint device_added_id;
};

static void     gsd_wacom_manager_class_init  (GsdWacomManagerClass *klass);
static void     gsd_wacom_manager_init        (GsdWacomManager      *wacom_manager);
static void     gsd_wacom_manager_finalize    (GObject              *object);
static void     set_wacom_settings            (GsdWacomManager      *manager);
static XDevice* device_is_wacom               (const gint            type,
                                               const XDeviceInfo deviceinfo);

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

static void
device_added_cb (GdkDeviceManager *device_manager,
                 GdkDevice        *device,
                 gpointer          user_data)
{
        set_wacom_settings ((GsdWacomManager *) user_data);
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
        manager->priv->device_manager = device_manager;
}

static gboolean
device_is_type (const gint type,
                const XDeviceInfo *dev)
{
        static Atom stylus, cursor, eraser, pad;

        if (!stylus)
                stylus = XInternAtom (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), "STYLUS", False);
        if (!eraser)
                eraser = XInternAtom (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), "ERASER", False);
        if (!cursor)
                cursor = XInternAtom (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), "CURSOR", False);
        if (!pad)
                pad = XInternAtom (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), "PAD", False);

        if ((type & WACOM_TYPE_STYLUS) && dev->type == stylus)
                return TRUE;

        if ((type & WACOM_TYPE_ERASER) && dev->type == eraser)
                return TRUE;

        if ((type & WACOM_TYPE_CURSOR) && dev->type == cursor)
                return TRUE;

        if ((type & WACOM_TYPE_PAD) && dev->type == pad)
                return TRUE;

         return FALSE;
}

static XDevice*
device_is_wacom (const gint type,
                 const XDeviceInfo deviceinfo)
{
        XDevice *device;
        Atom realtype, prop;
        int realformat;
        unsigned long nitems, bytes_after;
        unsigned char *data = NULL;
        int rc;

        if ((deviceinfo.use == IsXPointer) || (deviceinfo.use == IsXKeyboard))
                return NULL;

        if (!device_is_type (type, &deviceinfo))
                return NULL;

        /* There is currently no good way of detecting the driver for a device
         * other than checking for a driver-specific property.
         * Wacom Tool Type exists on all tools
         */ 
        prop = XInternAtom (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), "Wacom Tool Type", False);
        if (!prop)
                return NULL;

        gdk_error_trap_push ();
        device = XOpenDevice (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), deviceinfo.id);
        if (gdk_error_trap_pop () || (device == NULL))
                return NULL;

        gdk_error_trap_push ();

        rc = XGetDeviceProperty (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                                 device, prop, 0, 1, False,
                                 XA_ATOM, &realtype, &realformat, &nitems,
                                 &bytes_after, &data);
        if (gdk_error_trap_pop () || rc != Success || realtype == None) {
                XCloseDevice (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), device);
                device = NULL;
        }

        XFree (data);
        return device;
}

/* Generic property setting code. Fill up the struct property with the property
 * data and pass it into device_set_property together with the device to be
 * changed.  Note: doesn't cater for non-zero offsets yet, but we don't have
 * any settings that require that.
 */
struct property {
        const gchar *name;      /* property name */
        gint nitems;            /* number of items in data */
        gint format;            /* 8 or 32 */
        union {
                const gchar *c; /* 8 bit data */
                const gint *i;  /* 32 bit data */
        } data;
};

static void
device_set_property (XDevice            *device,
                     const char         *device_name,
                     struct property    *property)
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
                return;

        gdk_error_trap_push ();

        rc = XGetDeviceProperty (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                                 device, prop, 0, property->nitems, False,
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
                                       device, prop, XA_INTEGER, realformat,
                                       PropModeReplace, data, nitems);
        }

        if (gdk_error_trap_pop ())
                g_warning ("Error in setting \"%s\" for \"%s\"", property->name, device_name);
}

static void
wacom_set_property (gint wacom_type,
                    struct property *property)
{
        XDeviceInfo *device_info;
        gint n_devices;
        gint i;

        device_info = XListInputDevices (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), &n_devices);

        for (i = 0; i < n_devices; i++) {
                XDevice *device = NULL;

                device = device_is_wacom (wacom_type, device_info[i]);
                if (device == NULL)
                        continue;

                device_set_property (device, device_info[i].name, property);

                XCloseDevice (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), device);
        }

        if (device_info != NULL)
                XFreeDeviceList (device_info);
}

static void
set_rotation (GsdWacomRotation rotation)
{
        gchar rot = rotation;
        struct property property = {
                .name = "Wacom Rotation",
                .nitems = 1,
                .format = 8,
                .data.c = &rot,
        };

        wacom_set_property (WACOM_TYPE_ALL, &property);
}

static void
set_pressurecurve (const gint    wacom_type,
                   GVariant      *value)
{
        struct property property = {
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

        wacom_set_property (wacom_type, &property);
}

/* Area handling. Each area is defined as top x/y, bottom x/y and limits the
 * usable area of the physical device to the given area (in device coords)
 */
static void
set_area (const gint      wacom_type,
          GVariant        *value)
{
        struct property property = {
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

        wacom_set_property (wacom_type, &property);
}

static void
set_absolute (const gint wacom_type, gint is_absolute)
{
        XDeviceInfo *device_info;
        gint n_devices;
        gint i;

        device_info = XListInputDevices (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), &n_devices);

        for (i = 0; i < n_devices; i++) {
                XDevice *device = NULL;

                device = device_is_wacom (wacom_type, device_info[i]);
                if (device == NULL)
                        continue;

                gdk_error_trap_push ();
                XSetDeviceMode (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), device, is_absolute ? Absolute : Relative);
                if (gdk_error_trap_pop ())
                        g_error ("Failed to set mode \"%s\" for \"%s\".",
                                 is_absolute ? "Absolute" : "Relative", device_info[i].name);

                XCloseDevice (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), device);
        }

        if (device_info != NULL)
                XFreeDeviceList (device_info);
}

static void
set_device_buttonmap (gint wacom_type,
                      GVariant *value)
{
        XDeviceInfo *device_info;
        gint n_devices;
        gint i;
        gsize nmap;
        const gint *intmap;
        unsigned char map[WACOM_MAX_BUTTONS] = {};

        intmap = g_variant_get_fixed_array (value, &nmap, sizeof (gint32));
        for (i = 0; i < nmap && i < sizeof (map); i++)
                map[i] = intmap[i];

        device_info = XListInputDevices (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), &n_devices);

        for (i = 0; i < n_devices; i++) {
                XDevice *device = NULL;
                int rc, j;

                device = device_is_wacom (wacom_type, device_info[i]);
                if (device == NULL)
                        continue;

                gdk_error_trap_push ();

                /* X refuses to change the mapping while buttons are engaged,
                 * so if this is the case we'll retry a few times
                 */
                for (j = 0;
                     j < 20 && (rc = XSetDeviceButtonMapping (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), device, map, nmap)) == MappingBusy;
                     ++j) {
                        g_usleep (300);
                }

                if (gdk_error_trap_pop () || rc != Success)
                        g_warning ("Error in setting button mapping for \"%s\"", device_info->name);

                XCloseDevice (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), device);
        }

        if (device_info != NULL)
                XFreeDeviceList (device_info);
}

static void
set_touch (gboolean touch)
{
        gchar data = touch;
        struct property property = {
                .name = "Wacom Enable Touch",
                .nitems = 1,
                .format = 8,
                .data.c = &data,
        };

        wacom_set_property (WACOM_TYPE_ALL, &property);
}

static void
set_tpcbutton (gboolean tpcbutton)
{
        gchar data = tpcbutton;
        struct property property = {
                .name = "Wacom Hover Click",
                .nitems = 1,
                .format = 8,
                .data.c = &data,
        };

        wacom_set_property (WACOM_TYPE_ALL, &property);
}

static void
set_pressurethreshold (const gint wacom_type, gint threshold)
{
        struct property property = {
                .name = "Wacom Pressure Threshold",
                .nitems = 1,
                .format = 32,
                .data.i = &threshold,
        };

        wacom_set_property (wacom_type, &property);
}

static void
set_wacom_settings (GsdWacomManager *manager)
{
        set_rotation (g_settings_get_enum (manager->priv->wacom_settings, KEY_ROTATION));
        set_touch (g_settings_get_boolean (manager->priv->wacom_settings, KEY_TOUCH));
        set_tpcbutton (g_settings_get_boolean (manager->priv->wacom_settings, KEY_TPCBUTTON));

        /* only pen and eraser have pressure threshold and curve settings */
        set_pressurecurve (WACOM_TYPE_STYLUS,
                           g_settings_get_value (manager->priv->stylus_settings, KEY_PRESSURECURVE));
        set_pressurecurve (WACOM_TYPE_ERASER,
                           g_settings_get_value (manager->priv->eraser_settings, KEY_PRESSURECURVE));
        set_pressurethreshold (WACOM_TYPE_STYLUS,
                               g_settings_get_int (manager->priv->stylus_settings, KEY_PRESSURETHRESHOLD));
        set_pressurethreshold (WACOM_TYPE_ERASER,
                               g_settings_get_int (manager->priv->eraser_settings, KEY_PRESSURETHRESHOLD));

        set_absolute (WACOM_TYPE_STYLUS,
                      g_settings_get_boolean (manager->priv->stylus_settings, KEY_IS_ABSOLUTE));
        set_absolute (WACOM_TYPE_ERASER,
                      g_settings_get_boolean (manager->priv->eraser_settings, KEY_IS_ABSOLUTE));
        set_absolute (WACOM_TYPE_CURSOR,
                      g_settings_get_boolean (manager->priv->cursor_settings, KEY_IS_ABSOLUTE));
        /* pad can't be set to absolute */

        set_area (WACOM_TYPE_STYLUS,
                  g_settings_get_value (manager->priv->stylus_settings, KEY_AREA));
        set_area (WACOM_TYPE_ERASER,
                  g_settings_get_value (manager->priv->eraser_settings, KEY_AREA));
        set_area (WACOM_TYPE_CURSOR,
                  g_settings_get_value (manager->priv->cursor_settings, KEY_AREA));
        /* pad has no area */

        set_device_buttonmap (WACOM_TYPE_STYLUS,
                              g_settings_get_value (manager->priv->stylus_settings, KEY_BUTTONS));
        set_device_buttonmap (WACOM_TYPE_ERASER,
                              g_settings_get_value (manager->priv->eraser_settings, KEY_BUTTONS));
        set_device_buttonmap (WACOM_TYPE_PAD,
                              g_settings_get_value (manager->priv->pad_settings, KEY_BUTTONS));
        set_device_buttonmap (WACOM_TYPE_CURSOR,
                              g_settings_get_value (manager->priv->cursor_settings, KEY_BUTTONS));
}

static void
wacom_callback (GSettings          *settings,
                const gchar        *key,
                GsdWacomManager    *manager)
{
        set_wacom_settings (manager);
}

static void
gsd_wacom_manager_init (GsdWacomManager *manager)
{
        manager->priv = GSD_WACOM_MANAGER_GET_PRIVATE (manager);
}

static gboolean
gsd_wacom_manager_idle_cb (GsdWacomManager *manager)
{
        gnome_settings_profile_start (NULL);

        manager->priv->wacom_settings = g_settings_new (SETTINGS_WACOM_DIR);
        g_signal_connect (manager->priv->wacom_settings, "changed",
                          G_CALLBACK (wacom_callback), manager);

        manager->priv->stylus_settings = g_settings_new (SETTINGS_STYLUS_DIR);
        g_signal_connect (manager->priv->stylus_settings, "changed",
                          G_CALLBACK (wacom_callback), manager);

        manager->priv->eraser_settings = g_settings_new (SETTINGS_ERASER_DIR);
        g_signal_connect (manager->priv->eraser_settings, "changed",
                          G_CALLBACK (wacom_callback), manager);

        manager->priv->cursor_settings = g_settings_new (SETTINGS_CURSOR_DIR);
        g_signal_connect (manager->priv->cursor_settings, "changed",
                          G_CALLBACK (wacom_callback), manager);

        manager->priv->pad_settings = g_settings_new (SETTINGS_PAD_DIR);
        g_signal_connect (manager->priv->pad_settings, "changed",
                          G_CALLBACK (wacom_callback), manager);

        set_devicepresence_handler (manager);
        set_wacom_settings (manager);

        gnome_settings_profile_end (NULL);

        manager->priv->start_idle_id = 0;

        return FALSE;
}

gboolean
gsd_wacom_manager_start (GsdWacomManager *manager,
                         GError         **error)
{
        gnome_settings_profile_start (NULL);

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
                p->device_manager = NULL;
        }

        if (p->wacom_settings != NULL) {
                g_object_unref (p->wacom_settings);
                p->wacom_settings = NULL;
        }

        if (p->stylus_settings != NULL) {
                g_object_unref (p->stylus_settings);
                p->stylus_settings = NULL;
        }

        if (p->eraser_settings != NULL) {
                g_object_unref (p->eraser_settings);
                p->eraser_settings = NULL;
        }

        if (p->cursor_settings != NULL) {
                g_object_unref (p->cursor_settings);
                p->cursor_settings = NULL;
        }

        if (p->pad_settings != NULL) {
                g_object_unref (p->pad_settings);
                p->pad_settings = NULL;
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
