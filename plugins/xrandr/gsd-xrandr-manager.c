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
#include <gconf/gconf-client.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API

#include <libgnomeui/gnome-rr-config.h>
#include <libgnomeui/gnome-rr.h>

#ifdef HAVE_RANDR
#include <X11/extensions/Xrandr.h>
#endif

#include "gnome-settings-profile.h"
#include "gsd-xrandr-manager.h"

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX   255
#endif

#define GSD_XRANDR_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_XRANDR_MANAGER, GsdXrandrManagerPrivate))

#define CONF_DIR "/apps/gnome_settings_daemon/xrandr"
#define CONF_KEY "show_notification_icon"

#define VIDEO_KEYSYM    "XF86Display"

/* name of the icon files (gsd-xrandr.svg, etc.) */
#define GSD_XRANDR_ICON_NAME "gsd-xrandr"

/* executable of the control center's display configuration capplet */
#define GSD_XRANDR_DISPLAY_CAPPLET "gnome-display-properties"

struct GsdXrandrManagerPrivate
{
        /* Key code of the fn-F7 video key (XF86Display) */
        guint keycode;
        GnomeRRScreen *rw_screen;
        gboolean running;

        GtkStatusIcon *status_icon;
        GConfClient *client;
        int notify_id;
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
        GnomeRRScreen *screen = data;
        XEvent *ev = (XEvent *)xevent;

        if (ev->type == ClientMessage		&&
            ev->xclient.message_type == gnome_randr_xatom()) {

                gnome_rr_config_apply_stored (screen);

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
                gnome_rr_config_apply_stored (manager->priv->rw_screen);

                return GDK_FILTER_CONTINUE;
        }

        return GDK_FILTER_CONTINUE;
}

static void
on_randr_event (GnomeRRScreen *screen, gpointer data)
{
        GsdXrandrManager *manager = GSD_XRANDR_MANAGER (data);

        if (!manager->priv->running)
                return;

        /* FIXME: Set up any new screens here */
}

static void
popup_menu_configure_display_cb (GtkMenuItem *item, gpointer data)
{
        GdkScreen *screen;
        GError *error;

        screen = gtk_widget_get_screen (GTK_WIDGET (item));

        error = NULL;
        if (!gdk_spawn_command_line_on_screen (screen, GSD_XRANDR_DISPLAY_CAPPLET, &error)) {
		GtkWidget *dialog;

		dialog = gtk_message_dialog_new_with_markup (NULL, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                                             "<span weight=\"bold\" size=\"larger\">"
                                                             "Display configuration could not be run"
                                                             "</span>\n\n"
                                                             "%s", error->message);
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);

		g_error_free (error);
        }
}

static void
status_icon_popup_menu (GsdXrandrManager *manager, guint button, guint32 timestamp)
{
        struct GsdXrandrManagerPrivate *priv = manager->priv;
        GtkWidget *menu;
        GtkWidget *item;

        menu = gtk_menu_new ();

        item = gtk_menu_item_new_with_label (_("Screen Rotation"));
        gtk_widget_set_sensitive (item, FALSE);
        gtk_widget_show (item);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

        item = gtk_menu_item_new_with_mnemonic (_("_Configure Display Settings ..."));
        g_signal_connect (item, "activate",
                          G_CALLBACK (popup_menu_configure_display_cb), manager);
        gtk_widget_show (item);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
        /* FIXME */

        g_signal_connect (menu, "selection-done",
                          G_CALLBACK (gtk_widget_destroy), NULL);

        gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
                        gtk_status_icon_position_menu,
                        priv->status_icon, button, timestamp);
}

static void
status_icon_activate_cb (GtkStatusIcon *status_icon, gpointer data)
{
        GsdXrandrManager *manager = GSD_XRANDR_MANAGER (data);

        /* Suck; we don't get a proper button/timestamp */
        status_icon_popup_menu (manager, 0, gtk_get_current_event_time ());
}

static void
status_icon_popup_menu_cb (GtkStatusIcon *status_icon, guint button, guint32 timestamp, gpointer data)
{
        GsdXrandrManager *manager = GSD_XRANDR_MANAGER (data);

        status_icon_popup_menu (manager, button, timestamp);
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
        if (!priv->status_icon) {
                priv->status_icon = gtk_status_icon_new_from_icon_name (GSD_XRANDR_ICON_NAME);
                gtk_status_icon_set_tooltip (priv->status_icon, _("Configure display settings"));

                g_signal_connect (priv->status_icon, "activate",
                                  G_CALLBACK (status_icon_activate_cb), manager);
                g_signal_connect (priv->status_icon, "popup-menu",
                                  G_CALLBACK (status_icon_popup_menu_cb), manager);
        }
}

static void
status_icon_stop (GsdXrandrManager *manager)
{
        struct GsdXrandrManagerPrivate *priv = manager->priv;

        if (priv->status_icon) {
                g_signal_handlers_disconnect_by_func (
                        priv->status_icon, G_CALLBACK (status_icon_activate_cb), manager);
                g_signal_handlers_disconnect_by_func (
                        priv->status_icon, G_CALLBACK (status_icon_popup_menu_cb), manager);

                g_object_unref (priv->status_icon);
                priv->status_icon = NULL;
        }
}

static void
start_or_stop_icon (GsdXrandrManager *manager)
{
        if (gconf_client_get_bool (manager->priv->client, CONF_DIR "/" CONF_KEY, NULL)) {
                status_icon_start (manager);
        }
        else {
                status_icon_stop (manager);
        }
}

static void
on_config_changed (GConfClient          *client,
                   guint                 cnxn_id,
                   GConfEntry           *entry,
                   GsdXrandrManager *manager)
{
        start_or_stop_icon (manager);
}

gboolean
gsd_xrandr_manager_start (GsdXrandrManager *manager,
                          GError          **error)
{
        g_debug ("Starting xrandr manager");

        manager->priv->running = TRUE;
        manager->priv->client = gconf_client_get_default ();

        g_assert (manager->priv->notify_id == 0);

        gconf_client_add_dir (manager->priv->client, CONF_DIR,
                              GCONF_CLIENT_PRELOAD_NONE,
                              NULL);

        manager->priv->notify_id =
                gconf_client_notify_add (
                        manager->priv->client, CONF_DIR,
                        (GConfClientNotifyFunc)on_config_changed,
                        manager, NULL, NULL);

        if (manager->priv->keycode) {
                gdk_error_trap_push ();

                XGrabKey (gdk_x11_get_default_xdisplay(),
                          manager->priv->keycode, AnyModifier,
                          gdk_x11_get_default_root_xwindow(),
                          True, GrabModeAsync, GrabModeAsync);

                gdk_flush ();
                gdk_error_trap_pop ();
        }

        gnome_rr_config_apply_stored (manager->priv->rw_screen);

        gdk_window_add_filter (gdk_get_default_root_window(),
                               (GdkFilterFunc)event_filter,
                               manager);

        gdk_add_client_message_filter (gnome_randr_atom(),
                                       on_client_message,
                                       manager->priv->rw_screen);

        start_or_stop_icon (manager);

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

        if (manager->priv->notify_id != 0) {
                gconf_client_remove_dir (manager->priv->client,
                                         CONF_DIR, NULL);
                gconf_client_notify_remove (manager->priv->client,
                                            manager->priv->notify_id);
                manager->priv->notify_id = 0;
        }

        if (manager->priv->client != NULL) {
                g_object_unref (manager->priv->client);
                manager->priv->client = NULL;
        }

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
        manager->priv->rw_screen = gnome_rr_screen_new (
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
