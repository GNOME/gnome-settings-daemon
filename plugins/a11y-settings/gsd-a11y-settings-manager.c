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
#include <gio/gio.h>
#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-systemd.h>

#include "gnome-settings-profile.h"
#include "gnome-settings-systemd.h"
#include "gsd-a11y-settings-manager.h"

struct _GsdA11ySettingsManager
{
        GsdApplication parent;

        GDBusConnection *connection;
        GCancellable *orca_cancellable;

        GSettings *interface_settings;
        GSettings *a11y_apps_settings;

        GPid       fallback_orca;
};

enum {
        PROP_0,
};

static void     gsd_a11y_settings_manager_class_init  (GsdA11ySettingsManagerClass *klass);
static void     gsd_a11y_settings_manager_init        (GsdA11ySettingsManager      *a11y_settings_manager);

G_DEFINE_TYPE (GsdA11ySettingsManager, gsd_a11y_settings_manager, GSD_TYPE_APPLICATION)

static void
kill_fallback_orca (GsdA11ySettingsManager *manager)
{
        if (manager->fallback_orca == 0)
                return;

        if (kill(manager->fallback_orca, SIGTERM) < 0)
                g_warning ("Failed to kill fallback Orca (pid=%d): %m", manager->fallback_orca);
        else
                g_debug ("Killed fallback Orca (pid=%d)", manager->fallback_orca);

        manager->fallback_orca = 0;
}

static void
spawn_fallback_orca (GsdA11ySettingsManager *manager)
{
        char *argv[] = { "orca", "--replace", NULL };
        GDBusConnection *connection = g_application_get_dbus_connection (G_APPLICATION (manager));
        g_autoptr (GError) error = NULL;
        const gchar *workdir;
        GPid pid;
        gboolean success;

        g_warning ("Spawning fallback Orca");

        workdir = g_get_home_dir ();
        success = g_spawn_async (workdir, argv, NULL, G_SPAWN_SEARCH_PATH, NULL,
                                 NULL, &pid, &error);
        if (!success) {
                g_warning ("Failed to spawn fallback Orca: %s", error->message);
                return;
        }

        gnome_start_systemd_scope ("orca", pid, NULL, connection, NULL, NULL, NULL);

        g_debug ("Spawned fallback Orca (pid=%d)", pid);
        manager->fallback_orca = pid;
}

static void
unit_start_stop_cb (GObject      *source_object,
                    GAsyncResult *res,
                    gpointer      user_data)
{
        g_autoptr (GError) error = NULL;
        GsdA11ySettingsManager *manager = user_data;

        gnome_settings_systemd_manage_unit_finish (G_DBUS_CONNECTION (source_object),
                                                   res, &error);

        if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED))
                        g_warning ("%s", error->message);
                else
                        g_debug ("%s", error->message);

                if (manager)
                        spawn_fallback_orca (manager);
        }
}

static void
manage_orca (GsdA11ySettingsManager *manager,
             gboolean                enabled)
{
        kill_fallback_orca (manager);

        g_cancellable_cancel (manager->orca_cancellable);
        g_clear_object (&manager->orca_cancellable);
        manager->orca_cancellable = g_cancellable_new ();

        gnome_settings_systemd_manage_unit (manager->connection,
                                            "orca.service",
                                            enabled,
                                            TRUE,
                                            manager->orca_cancellable,
                                            unit_start_stop_cb,
                                            enabled ? manager : NULL);
}

static void
apps_settings_changed (GSettings              *settings,
                       const char             *key,
                       GsdA11ySettingsManager *manager)
{
        gboolean screen_reader_changed, keyboard_changed, magnifier_changed;
        gboolean screen_reader_enabled, keyboard_enabled, magnifier_enabled;

        screen_reader_changed = g_str_equal (key, "screen-reader-enabled");
        keyboard_changed = g_str_equal (key, "screen-keyboard-enabled");
        magnifier_changed = g_str_equal (key, "screen-magnifier-enabled");

        if (!screen_reader_changed && !keyboard_changed && !magnifier_changed)
                return;

        g_debug ("screen reader, OSK or magnifier enablement changed");

        screen_reader_enabled = g_settings_get_boolean (settings, "screen-reader-enabled");
        keyboard_enabled = g_settings_get_boolean (settings, "screen-keyboard-enabled");
        magnifier_enabled = g_settings_get_boolean (settings, "screen-magnifier-enabled");

        if (screen_reader_enabled || keyboard_enabled || magnifier_enabled) {
                g_debug ("Enabling toolkit-accessibility, screen reader, OSK or magnifier enabled");
                g_settings_set_boolean (manager->interface_settings, "toolkit-accessibility", TRUE);
        } else {
                g_debug ("Disabling toolkit-accessibility, screen reader, OSK and magnifier disabled");
                g_settings_set_boolean (manager->interface_settings, "toolkit-accessibility", FALSE);
        }

        if (screen_reader_changed)
                manage_orca (manager, screen_reader_enabled);
}

static void
gsd_a11y_settings_manager_startup (GApplication *app)
{
        g_autoptr (GError) error = NULL;
        GsdA11ySettingsManager *manager = GSD_A11Y_SETTINGS_MANAGER (app);
        gboolean screen_reader_enabled;

        g_debug ("Starting a11y_settings manager");
        gnome_settings_profile_start (NULL);

        manager->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
        if (!manager->connection) {
                    g_warning ("Failed to obtain session bus: %s", error->message);
                    g_clear_error (&error);
        }

        manager->interface_settings = g_settings_new ("org.gnome.desktop.interface");
        manager->a11y_apps_settings = g_settings_new ("org.gnome.desktop.a11y.applications");

        g_signal_connect (G_OBJECT (manager->a11y_apps_settings), "changed",
                          G_CALLBACK (apps_settings_changed), manager);

        /* If any of the screen reader, on-screen keyboard or magnifier are
         * enabled, make sure a11y is enabled for the toolkits.
         * We don't do the same thing for the reverse so it's possible to
         * enable AT-SPI for the toolkits without using an a11y app */
        screen_reader_enabled = g_settings_get_boolean (manager->a11y_apps_settings, "screen-reader-enabled");
        manage_orca (manager, screen_reader_enabled);
        if (g_settings_get_boolean (manager->a11y_apps_settings, "screen-keyboard-enabled") ||
            g_settings_get_boolean (manager->a11y_apps_settings, "screen-magnifier-enabled") ||
            screen_reader_enabled)
                g_settings_set_boolean (manager->interface_settings, "toolkit-accessibility", TRUE);

        G_APPLICATION_CLASS (gsd_a11y_settings_manager_parent_class)->startup (app);

        gnome_settings_profile_end (NULL);
}

static void
gsd_a11y_settings_manager_shutdown (GApplication *app)
{
        GsdA11ySettingsManager *manager = GSD_A11Y_SETTINGS_MANAGER (app);

        kill_fallback_orca (manager);

        g_clear_object (&manager->connection);
        g_clear_object (&manager->interface_settings);
        g_clear_object (&manager->a11y_apps_settings);

        G_APPLICATION_CLASS (gsd_a11y_settings_manager_parent_class)->shutdown (app);

        g_debug ("Stopping a11y_settings manager");
}

static void
gsd_a11y_settings_manager_class_init (GsdA11ySettingsManagerClass *klass)
{
        GApplicationClass *application_class = G_APPLICATION_CLASS (klass);

        application_class->startup = gsd_a11y_settings_manager_startup;
        application_class->shutdown = gsd_a11y_settings_manager_shutdown;
}

static void
gsd_a11y_settings_manager_init (GsdA11ySettingsManager *manager)
{
}
