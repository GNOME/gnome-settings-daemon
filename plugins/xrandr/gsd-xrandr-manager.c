/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
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
#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>

#ifdef HAVE_RANDR
#include <X11/extensions/Xrandr.h>
#endif

#include "gsd-xrandr-manager.h"

static void     gsd_xrandr_manager_class_init  (GsdXrandrManagerClass *klass);
static void     gsd_xrandr_manager_init        (GsdXrandrManager      *xrandr_manager);
static void     gsd_xrandr_manager_finalize    (GObject             *object);

G_DEFINE_TYPE (GsdXrandrManager, gsd_xrandr_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

#ifdef HAVE_RANDR
static int
get_rotation (GConfClient *client,
              char        *display,
              int          screen)
{
        char   *key;
        int     val;
        GError *error;

        key = g_strdup_printf ("%s/%d/rotation", display, screen);
        error = NULL;
        val = gconf_client_get_int (client, key, &error);
        g_free (key);

        if (error == NULL) {
                return val;
        }

        g_error_free (error);

        return 0;
}

static int
get_resolution (GConfClient *client,
                int          screen,
                char        *keys[],
                int         *width,
                int         *height)
{
        int   i;
        char *key;
        char *val;
        int   w;
        int   h;

        val = NULL;
        for (i = 0; keys[i] != NULL; i++) {
                key = g_strdup_printf ("%s/%d/resolution", keys[i], screen);
                val = gconf_client_get_string (client, key, NULL);
                g_free (key);

                if (val != NULL) {
                        break;
                }
        }

        if (val == NULL) {
                return -1;
        }

        if (sscanf (val, "%dx%d", &w, &h) != 2) {
                g_free (val);
                return -1;
        }

        g_free (val);

        *width = w;
        *height = h;

        return i;
}

static int
get_rate (GConfClient *client,
          char        *display,
          int          screen)
{
        char   *key;
        int     val;
        GError *error;

        key = g_strdup_printf ("%s/%d/rate", display, screen);
        error = NULL;
        val = gconf_client_get_int (client, key, &error);
        g_free (key);

        if (error == NULL) {
                return val;
        }

        g_error_free (error);

        return 0;
}

static int
find_closest_size (XRRScreenSize *sizes,
                   int            nsizes,
                   int            width,
                   int            height)
{
        int closest;
        int closest_width;
        int closest_height;
        int i;

        closest = 0;
        closest_width = sizes[0].width;
        closest_height = sizes[0].height;
        for (i = 1; i < nsizes; i++) {
                if (ABS (sizes[i].width - width) < ABS (closest_width - width) ||
                    (sizes[i].width == closest_width &&
                     ABS (sizes[i].height - height) < ABS (closest_height - height))) {
                        closest = i;
                        closest_width = sizes[i].width;
                        closest_height = sizes[i].height;
                }
        }

        return closest;
}
#endif /* HAVE_RANDR */

static void
apply_settings (GsdXrandrManager *manager)
{
#ifdef HAVE_RANDR
        GdkDisplay  *display;
        Display     *xdisplay;
        int          major;
        int          minor;
        int          event_base;
        int          error_base;
        GConfClient *client;
        int          n_screens;
        GdkScreen   *screen;
        GdkWindow   *root_window;
        int          width;
        int          height;
        int          rate;
        int          rotation;
#ifdef HOST_NAME_MAX
        char         hostname[HOST_NAME_MAX + 1];
#else
        char         hostname[256];
#endif
        char        *specific_path;
        char        *keys[3];
        int          i;
        int          residx;

        display = gdk_display_get_default ();
        xdisplay = gdk_x11_display_get_xdisplay (display);

        /* Check if XRandR is supported on the display */
        if (!XRRQueryExtension (xdisplay, &event_base, &error_base)
            || XRRQueryVersion (xdisplay, &major, &minor) == 0) {
                return;
        }

        if (major != 1 || minor < 1) {
                g_message ("Display has unsupported version of XRandR (%d.%d), not setting resolution.", major, minor);
                return;
        }

        client = gconf_client_get_default ();

        i = 0;
        specific_path = NULL;
        if (gethostname (hostname, sizeof (hostname)) == 0) {
                specific_path = g_strconcat ("/desktop/gnome/screen/", hostname,  NULL);
                keys[i++] = specific_path;
        }
        keys[i++] = "/desktop/gnome/screen/default";
        keys[i++] = NULL;

        n_screens = gdk_display_get_n_screens (display);
        for (i = 0; i < n_screens; i++) {
                screen = gdk_display_get_screen (display, i);
                root_window = gdk_screen_get_root_window (screen);
                residx = get_resolution (client, i, keys, &width, &height);

                if (residx != -1) {
                        XRRScreenSize          *sizes;
                        int                     nsizes;
                        int                     j;
                        int                     closest;
                        short                  *rates;
                        int                     nrates;
                        int                     status;
                        int                     current_size;
                        short                   current_rate;
                        XRRScreenConfiguration *config;
                        Rotation                current_rotation;

                        config = XRRGetScreenInfo (xdisplay, gdk_x11_drawable_get_xid (GDK_DRAWABLE (root_window)));

                        rate = get_rate (client, keys[residx], i);

                        sizes = XRRConfigSizes (config, &nsizes);
                        closest = find_closest_size (sizes, nsizes, width, height);

                        rates = XRRConfigRates (config, closest, &nrates);
                        for (j = 0; j < nrates; j++) {
                                if (rates[j] == rate)
                                        break;
                        }

                        /* Rate not supported, let X pick */
                        if (j == nrates)
                                rate = 0;

                        rotation = get_rotation (client, keys[residx], i);
                        if (rotation == 0)
                                rotation = RR_Rotate_0;

                        current_size = XRRConfigCurrentConfiguration (config, &current_rotation);
                        current_rate = XRRConfigCurrentRate (config);

                        if (closest != current_size ||
                            rate != current_rate ||
                            rotation != current_rotation) {
                                status = XRRSetScreenConfigAndRate (xdisplay,
                                                                    config,
                                                                    gdk_x11_drawable_get_xid (GDK_DRAWABLE (root_window)),
                                                                    closest,
                                                                    (Rotation) rotation,
                                                                    rate,
                                                                    GDK_CURRENT_TIME);
                        }

                        XRRFreeScreenConfigInfo (config);
                }
        }

        g_free (specific_path);

        /* We need to make sure we process the screen resize event. */
        gdk_display_sync (display);

        while (gtk_events_pending ()) {
                gtk_main_iteration ();
        }

        if (client != NULL) {
                g_object_unref (client);
        }

#endif /* HAVE_RANDR */
}

gboolean
gsd_xrandr_manager_start (GsdXrandrManager *manager,
                          GError          **error)
{
        g_debug ("Starting xrandr manager");

        apply_settings (manager);

        return TRUE;
}

void
gsd_xrandr_manager_stop (GsdXrandrManager *manager)
{
        g_debug ("Stopping xrandr manager");
}

static void
gsd_xrandr_manager_set_property (GObject        *object,
                               guint           prop_id,
                               const GValue   *value,
                               GParamSpec     *pspec)
{
        GsdXrandrManager *self;

        self = GSD_XRANDR_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsd_xrandr_manager_get_property (GObject        *object,
                               guint           prop_id,
                               GValue         *value,
                               GParamSpec     *pspec)
{
        GsdXrandrManager *self;

        self = GSD_XRANDR_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gsd_xrandr_manager_constructor (GType                  type,
                              guint                  n_construct_properties,
                              GObjectConstructParam *construct_properties)
{
        GsdXrandrManager      *xrandr_manager;
        GsdXrandrManagerClass *klass;

        klass = GSD_XRANDR_MANAGER_CLASS (g_type_class_peek (GSD_TYPE_XRANDR_MANAGER));

        xrandr_manager = GSD_XRANDR_MANAGER (G_OBJECT_CLASS (gsd_xrandr_manager_parent_class)->constructor (type,
                                                                                                      n_construct_properties,
                                                                                                      construct_properties));

        return G_OBJECT (xrandr_manager);
}

static void
gsd_xrandr_manager_dispose (GObject *object)
{
        GsdXrandrManager *xrandr_manager;

        xrandr_manager = GSD_XRANDR_MANAGER (object);

        G_OBJECT_CLASS (gsd_xrandr_manager_parent_class)->dispose (object);
}

static void
gsd_xrandr_manager_class_init (GsdXrandrManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsd_xrandr_manager_get_property;
        object_class->set_property = gsd_xrandr_manager_set_property;
        object_class->constructor = gsd_xrandr_manager_constructor;
        object_class->dispose = gsd_xrandr_manager_dispose;
        object_class->finalize = gsd_xrandr_manager_finalize;
}

static void
gsd_xrandr_manager_init (GsdXrandrManager *manager)
{
}

static void
gsd_xrandr_manager_finalize (GObject *object)
{
        GsdXrandrManager *xrandr_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_XRANDR_MANAGER (object));

        xrandr_manager = GSD_XRANDR_MANAGER (object);

        G_OBJECT_CLASS (gsd_xrandr_manager_parent_class)->finalize (object);
}

GsdXrandrManager *
gsd_xrandr_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_XRANDR_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_XRANDR_MANAGER (manager_object);
}
