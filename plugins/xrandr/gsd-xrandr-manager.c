/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2007, 2008 Red Hat, Inc
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

#define GNOME_DESKTOP_USE_UNSTABLE_API

#include <libgnomeui/monitor-db.h>
#include <libgnomeui/randrwrap.h>

#ifdef HAVE_RANDR
#include <X11/extensions/Xrandr.h>
#endif

#include "gnome-settings-profile.h"
#include "gsd-xrandr-manager.h"

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX   255
#endif

#define GSD_XRANDR_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_XRANDR_MANAGER, GsdXrandrManagerPrivate))

#define VIDEO_KEYSYM    "XF86Display"

/* name of the icon files (gsd-xrandr.svg, etc.) */
#define GSD_XRANDR_ICON_NAME "gsd-xrandr"

struct GsdXrandrManagerPrivate
{
        /* Key code of the fn-F7 video key (XF86Display) */
        guint keycode;
        RWScreen *rw_screen;
        gboolean running;

        GtkStatusIcon *status_icon;
};

enum {
        PROP_0,
};

static void     gsd_xrandr_manager_class_init  (GsdXrandrManagerClass *klass);
static void     gsd_xrandr_manager_init        (GsdXrandrManager      *xrandr_manager);
static void     gsd_xrandr_manager_finalize    (GObject             *object);

G_DEFINE_TYPE (GsdXrandrManager, gsd_xrandr_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;


static GdkAtom
gnome_randr_atom (void)
{
        return gdk_atom_intern ("_GNOME_RANDR_ATOM", FALSE);
}

static Atom
gnome_randr_xatom (void)
{
        return gdk_x11_atom_to_xatom (gnome_randr_atom());
}

static GdkFilterReturn
on_client_message (GdkXEvent  *xevent,
		   GdkEvent   *event,
		   gpointer    data)
{
        RWScreen *screen = data;
        XEvent *ev = (XEvent *)xevent;
        
        if (ev->type == ClientMessage		&&
            ev->xclient.message_type == gnome_randr_xatom()) {
                
                configuration_apply_stored (screen);
                
                return GDK_FILTER_REMOVE;
        }
        
        /* Pass the event on to GTK+ */
        return GDK_FILTER_CONTINUE;
}

static GdkFilterReturn
event_filter (GdkXEvent           *xevent,
              GdkEvent            *event,
              gpointer             data)
{
        GsdXrandrManager *manager = data;
        XEvent *xev = (XEvent *) xevent;

        if (!manager->priv->running)
                return GDK_FILTER_CONTINUE;

        /* verify we have a key event */
        if (xev->xany.type != KeyPress && xev->xany.type != KeyRelease)
                return GDK_FILTER_CONTINUE;

        if (xev->xkey.keycode == manager->priv->keycode) {
                /* FIXME: here we should cycle between valid
                 * configurations, and save them
                 */
                configuration_apply_stored (manager->priv->rw_screen);
                
                return GDK_FILTER_CONTINUE;
        }

        return GDK_FILTER_CONTINUE;
}

static void
on_randr_event (RWScreen *screen, gpointer data)
{
        GsdXrandrManager *manager = data;

        if (!manager->priv->running)
                return;
        
        /* FIXME: Set up any new screens here */
}

static void
status_icon_start (GsdXrandrManager *manager)
{
        struct GsdXrandrManagerPrivate *priv = manager->priv;

        /* FIXME: We may want to make this icon optional (with a GConf key,
         * toggled from a checkbox in gnome-display-properties.
         *
         * Or ideally, we should detect if we are on a tablet and only display
         * the icon in that case.
         */

        priv->status_icon = gtk_status_icon_new_from_icon_name (GSD_XRANDR_ICON_NAME);
        gtk_status_icon_set_tooltip (priv->status_icon, _("Configure display settings"));
}

static void
status_icon_stop (GsdXrandrManager *manager)
{
        struct GsdXrandrManagerPrivate *priv = manager->priv;

        g_object_unref (priv->status_icon);
        priv->status_icon = NULL;
}

gboolean
gsd_xrandr_manager_start (GsdXrandrManager *manager,
                          GError          **error)
{
        g_debug ("Starting xrandr manager");

        manager->priv->running = TRUE;
        
        if (manager->priv->keycode) {
                gdk_error_trap_push ();
                
                XGrabKey (gdk_x11_get_default_xdisplay(),
                          manager->priv->keycode, AnyModifier,
                          gdk_x11_get_default_root_xwindow(),
                          True, GrabModeAsync, GrabModeAsync);

                gdk_flush ();
                gdk_error_trap_pop ();
        }
        
        configuration_apply_stored (manager->priv->rw_screen);
        
        gdk_window_add_filter (gdk_get_default_root_window(),
                               (GdkFilterFunc)event_filter,
                               manager);
        
        gdk_add_client_message_filter (gnome_randr_atom(),
                                       on_client_message,
                                       manager->priv->rw_screen);

        status_icon_start (manager);
        
        return TRUE;
}

void
gsd_xrandr_manager_stop (GsdXrandrManager *manager)
{
        g_debug ("Stopping xrandr manager");

        manager->priv->running = FALSE;
        
        gdk_error_trap_push ();
        
        XUngrabKey (gdk_x11_get_default_xdisplay(),
                    manager->priv->keycode, AnyModifier,
                    gdk_x11_get_default_root_xwindow());

        gdk_error_trap_pop ();

        status_icon_stop (manager);
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
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsd_xrandr_manager_get_property;
        object_class->set_property = gsd_xrandr_manager_set_property;
        object_class->constructor = gsd_xrandr_manager_constructor;
        object_class->dispose = gsd_xrandr_manager_dispose;
        object_class->finalize = gsd_xrandr_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdXrandrManagerPrivate));
}

static void
gsd_xrandr_manager_init (GsdXrandrManager *manager)
{
        Display *dpy = gdk_x11_get_default_xdisplay ();
        guint keyval = gdk_keyval_from_name (VIDEO_KEYSYM);
        guint keycode = XKeysymToKeycode (dpy, keyval);
        
        manager->priv = GSD_XRANDR_MANAGER_GET_PRIVATE (manager);

        manager->priv->keycode = keycode;
        manager->priv->rw_screen = rw_screen_new (
                gdk_screen_get_default(), on_randr_event, NULL);
}

static void
gsd_xrandr_manager_finalize (GObject *object)
{
        GsdXrandrManager *xrandr_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_XRANDR_MANAGER (object));

        xrandr_manager = GSD_XRANDR_MANAGER (object);

        g_return_if_fail (xrandr_manager->priv != NULL);

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
