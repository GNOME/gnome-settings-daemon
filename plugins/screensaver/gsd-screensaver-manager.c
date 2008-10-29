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

#include "gnome-settings-profile.h"
#include "gsd-screensaver-manager.h"

#define START_SCREENSAVER_KEY   "/apps/gnome_settings_daemon/screensaver/start_screensaver"
#define SHOW_STARTUP_ERRORS_KEY "/apps/gnome_settings_daemon/screensaver/show_startup_errors"

#define GSD_SCREENSAVER_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_SCREENSAVER_MANAGER, GsdScreensaverManagerPrivate))

struct GsdScreensaverManagerPrivate
{
        GPid     screensaver_pid;
        gboolean start_screensaver;
        gboolean have_gscreensaver;
        gboolean have_xscreensaver;
};

static void     gsd_screensaver_manager_class_init  (GsdScreensaverManagerClass *klass);
static void     gsd_screensaver_manager_init        (GsdScreensaverManager      *screensaver_manager);
static void     gsd_screensaver_manager_finalize    (GObject             *object);

G_DEFINE_TYPE (GsdScreensaverManager, gsd_screensaver_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static void
key_toggled_cb (GtkWidget             *toggle,
                GsdScreensaverManager *manager)
{
        GConfClient *client;

        client = gconf_client_get_default ();
        gconf_client_set_bool (client,
                               SHOW_STARTUP_ERRORS_KEY,
                               gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (toggle))
                               ? 0 : 1,
                               NULL);
        g_object_unref (client);
}

static gboolean
start_screensaver_idle_cb (GsdScreensaverManager *manager)
{
        GError      *error = NULL;
        char        *ss_cmd;
        gboolean     show_error;
        char        *args[3];
        GConfClient *client;

        g_debug ("Starting screensaver process in idle callback");
        gnome_settings_profile_start (NULL);

        client = gconf_client_get_default ();

        manager->priv->start_screensaver = gconf_client_get_bool (client, START_SCREENSAVER_KEY, NULL);
        if (!manager->priv->start_screensaver)
                return FALSE;

        if ((ss_cmd = g_find_program_in_path ("gnome-screensaver"))) {
                manager->priv->have_gscreensaver = TRUE;
                g_free (ss_cmd);
        } else {
                manager->priv->have_gscreensaver = FALSE;
        }

        if ((ss_cmd = g_find_program_in_path ("xscreensaver"))) {
                manager->priv->have_xscreensaver = TRUE;
                g_free (ss_cmd);
        } else {
                manager->priv->have_xscreensaver = FALSE;
        }

        if (manager->priv->have_gscreensaver) {
                args[0] = "gnome-screensaver";
                args[1] = NULL;
        } else if (manager->priv->have_xscreensaver) {
                args[0] = "xscreensaver";
                args[1] = "-nosplash";
        } else {
                g_warning ("No screensaver available");
                return FALSE;
        }
        args[2] = NULL;

        if (g_spawn_async (g_get_home_dir (), args, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &manager->priv->screensaver_pid, &error)) {
                g_object_unref (client);
                return FALSE;
        }

        show_error = gconf_client_get_bool (client, SHOW_STARTUP_ERRORS_KEY, NULL);

        if (show_error) {
                GtkWidget *dialog;
                GtkWidget *toggle;

	        dialog = gtk_message_dialog_new (NULL,
                             0, GTK_MESSAGE_ERROR,
                             GTK_BUTTONS_OK,
                             _("There was an error starting up the screensaver:\n\n"
                               "%s\n\n"
                               "Screensaver functionality will not work in this session."),
                             error->message);

                g_error_free (error);

                g_signal_connect (dialog, "response",
                                  G_CALLBACK (gtk_widget_destroy),
                                  NULL);

                toggle = gtk_check_button_new_with_mnemonic (_("_Do not show this message again"));
                gtk_widget_show (toggle);

                if (gconf_client_key_is_writable (client, SHOW_STARTUP_ERRORS_KEY, NULL)) {
                        g_signal_connect (toggle,
                                          "toggled",
                                          G_CALLBACK (key_toggled_cb),
                                          manager);
                } else {
                        gtk_widget_set_sensitive (toggle, FALSE);
                }

                gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
                                    toggle,
                                    FALSE, FALSE, 0);

                gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

                gtk_widget_show (dialog);
        }

        g_object_unref (client);

        gnome_settings_profile_end (NULL);

        return FALSE;
}

gboolean
gsd_screensaver_manager_start (GsdScreensaverManager *manager,
                               GError               **error)
{
        g_debug ("Starting screensaver manager");
        gnome_settings_profile_start (NULL);

        /*
	 * with gnome-screensaver, all settings are loaded internally
	 * from gconf at startup
	 *
	 * with xscreensaver, our settings only apply to startup, and
	 * the screensaver settings are all in xscreensaver and not
	 * gconf.
	 *
	 * we could have xscreensaver-demo run gconftool-2 directly,
	 * and start / stop xscreensaver here
         */

        g_idle_add ((GSourceFunc) start_screensaver_idle_cb, manager);

        gnome_settings_profile_end (NULL);

        return FALSE;
}

void
gsd_screensaver_manager_stop (GsdScreensaverManager *manager)
{
        g_debug ("Stopping screensaver manager");

        g_spawn_close_pid (manager->priv->screensaver_pid);
}

static void
gsd_screensaver_manager_set_property (GObject        *object,
                               guint           prop_id,
                               const GValue   *value,
                               GParamSpec     *pspec)
{
        GsdScreensaverManager *self;

        self = GSD_SCREENSAVER_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsd_screensaver_manager_get_property (GObject        *object,
                               guint           prop_id,
                               GValue         *value,
                               GParamSpec     *pspec)
{
        GsdScreensaverManager *self;

        self = GSD_SCREENSAVER_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gsd_screensaver_manager_constructor (GType                  type,
                              guint                  n_construct_properties,
                              GObjectConstructParam *construct_properties)
{
        GsdScreensaverManager      *screensaver_manager;
        GsdScreensaverManagerClass *klass;

        klass = GSD_SCREENSAVER_MANAGER_CLASS (g_type_class_peek (GSD_TYPE_SCREENSAVER_MANAGER));

        screensaver_manager = GSD_SCREENSAVER_MANAGER (G_OBJECT_CLASS (gsd_screensaver_manager_parent_class)->constructor (type,
                                                                                                      n_construct_properties,
                                                                                                      construct_properties));

        return G_OBJECT (screensaver_manager);
}

static void
gsd_screensaver_manager_dispose (GObject *object)
{
        GsdScreensaverManager *screensaver_manager;

        screensaver_manager = GSD_SCREENSAVER_MANAGER (object);

        G_OBJECT_CLASS (gsd_screensaver_manager_parent_class)->dispose (object);
}

static void
gsd_screensaver_manager_class_init (GsdScreensaverManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsd_screensaver_manager_get_property;
        object_class->set_property = gsd_screensaver_manager_set_property;
        object_class->constructor = gsd_screensaver_manager_constructor;
        object_class->dispose = gsd_screensaver_manager_dispose;
        object_class->finalize = gsd_screensaver_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdScreensaverManagerPrivate));
}

static void
gsd_screensaver_manager_init (GsdScreensaverManager *manager)
{
        manager->priv = GSD_SCREENSAVER_MANAGER_GET_PRIVATE (manager);
}

static void
gsd_screensaver_manager_finalize (GObject *object)
{
        GsdScreensaverManager *screensaver_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_SCREENSAVER_MANAGER (object));

        screensaver_manager = GSD_SCREENSAVER_MANAGER (object);

        g_return_if_fail (screensaver_manager->priv != NULL);

        G_OBJECT_CLASS (gsd_screensaver_manager_parent_class)->finalize (object);
}

GsdScreensaverManager *
gsd_screensaver_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_SCREENSAVER_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_SCREENSAVER_MANAGER (manager_object);
}
