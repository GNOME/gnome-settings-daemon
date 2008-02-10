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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <stdlib.h>
#include <unistd.h>
#include <libintl.h>
#include <errno.h>
#include <locale.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libgnome/libgnome.h>

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gnome-settings-manager.h"

#define GSD_DBUS_NAME         "org.gnome.SettingsDaemon"

#define DEFAULT_GCONF_PREFIX "/apps/gnome_settings_daemon/plugins"
#define GCONF_PREFIX_ENV     "GNOME_SETTINGS_DAEMON_GCONF_PREFIX"

static char      *gconf_prefix = NULL;
static gboolean   no_daemon    = TRUE;

static GOptionEntry entries[] = {
        {"no-daemon", 0, 0, G_OPTION_ARG_NONE, &no_daemon, N_("Don't become a daemon"), NULL },
        {"gconf-prefix", 0, 0, G_OPTION_ARG_STRING, &gconf_prefix, "GConf prefix from which to load plugin settings", NULL},
        {NULL}
};

static DBusGProxy *
get_bus_proxy (DBusGConnection *connection)
{
        DBusGProxy *bus_proxy;

        bus_proxy = dbus_g_proxy_new_for_name (connection,
                                               DBUS_SERVICE_DBUS,
                                               DBUS_PATH_DBUS,
                                               DBUS_INTERFACE_DBUS);
        return bus_proxy;
}

static gboolean
acquire_name_on_proxy (DBusGProxy *bus_proxy)
{
        GError     *error;
        guint       result;
        gboolean    res;
        gboolean    ret;

        ret = FALSE;

        if (bus_proxy == NULL) {
                goto out;
        }

        error = NULL;
        res = dbus_g_proxy_call (bus_proxy,
                                 "RequestName",
                                 &error,
                                 G_TYPE_STRING, GSD_DBUS_NAME,
                                 G_TYPE_UINT, 0,
                                 G_TYPE_INVALID,
                                 G_TYPE_UINT, &result,
                                 G_TYPE_INVALID);
        if (! res) {
                if (error != NULL) {
                        g_warning ("Failed to acquire %s: %s", GSD_DBUS_NAME, error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to acquire %s", GSD_DBUS_NAME);
                }
                goto out;
        }

        if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
                if (error != NULL) {
                        g_warning ("Failed to acquire %s: %s", GSD_DBUS_NAME, error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to acquire %s", GSD_DBUS_NAME);
                }
                goto out;
        }

        ret = TRUE;

 out:
        return ret;
}

static DBusGConnection *
get_session_bus (void)
{
        GError          *error;
        DBusGConnection *bus;
        DBusConnection  *connection;

        error = NULL;
        bus = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
        if (bus == NULL) {
                g_warning ("Couldn't connect to session bus: %s",
                           error->message);
                g_error_free (error);
                goto out;
        }

        connection = dbus_g_connection_get_connection (bus);
        dbus_connection_set_exit_on_disconnect (connection, TRUE);

 out:
        return bus;
}

static gboolean
bus_register (void)
{
        DBusGConnection *bus;
        DBusGProxy      *bus_proxy;
        gboolean         ret;

        ret = FALSE;

        bus = get_session_bus ();
        if (bus == NULL) {
                g_warning ("Could not get a connection to the bus");
                goto out;
        }

        bus_proxy = get_bus_proxy (bus);
        if (bus_proxy == NULL) {
                g_warning ("Could not construct bus_proxy object");
                goto out;
        }

        if (! acquire_name_on_proxy (bus_proxy)) {
                g_warning ("Could not acquire name");
                goto out;
        }

        g_debug ("Successfully connected to D-Bus");

        ret = TRUE;

 out:
        return ret;
}

int
main (int argc, char *argv[])
{
        GnomeSettingsManager *manager;
        GnomeProgram         *program;
        gboolean              res;
        GError               *error;

        manager = NULL;
        program = NULL;

        bindtextdomain (GETTEXT_PACKAGE, GNOME_SETTINGS_LOCALEDIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        setlocale (LC_ALL, "");

        g_type_init ();

        error = NULL;
        if (! gtk_init_with_args (&argc, &argv, NULL, entries, NULL, &error)) {
                if (error != NULL) {
                        g_warning ("%s", error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Unable to initialize GTK+");
                }
                exit (1);
        }

        if (gconf_prefix == NULL) {
                gconf_prefix = g_strdup (g_getenv (GCONF_PREFIX_ENV));
                if (gconf_prefix == NULL) {
                        gconf_prefix = g_strdup (DEFAULT_GCONF_PREFIX);
                }
        }

        if (! no_daemon && daemon (0, 0)) {
                g_error ("Could not daemonize: %s", g_strerror (errno));
        }

        if (! bus_register ()) {
                goto out;
        }

        program = gnome_program_init(
                PACKAGE, VERSION, LIBGNOME_MODULE,
                argc, argv, GNOME_PARAM_NONE);

        manager = gnome_settings_manager_new (gconf_prefix);

        res = gnome_settings_manager_start (manager, &error);
        if (! res) {
                g_warning ("Unable to start: %s", error->message);
                g_error_free (error);
                goto out;
        }

        gtk_main ();

        g_free (gconf_prefix);

 out:
        if (manager != NULL) {
                g_object_unref (manager);
        }

        if (program != NULL) {
                g_object_unref (program);
        }

        g_debug ("SettingsDaemon finished");

        return 0;
}
