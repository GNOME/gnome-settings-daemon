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

#include "gnome-settings-profile.h"
#include "gsd-a11y-settings-manager.h"

struct _GsdA11ySettingsManager
{
        GsdApplication parent;

        GSettings *interface_settings;
        GSettings *a11y_apps_settings;
};

enum {
        PROP_0,
};

static void     gsd_a11y_settings_manager_class_init  (GsdA11ySettingsManagerClass *klass);
static void     gsd_a11y_settings_manager_init        (GsdA11ySettingsManager      *a11y_settings_manager);

G_DEFINE_TYPE (GsdA11ySettingsManager, gsd_a11y_settings_manager, GSD_TYPE_APPLICATION)

static void
apps_settings_changed (GSettings              *settings,
		       const char             *key,
		       GsdA11ySettingsManager *manager)
{
	gboolean screen_reader, keyboard, magnifier;

	if (g_str_equal (key, "screen-reader-enabled") == FALSE &&
	    g_str_equal (key, "screen-keyboard-enabled") == FALSE &&
	    g_str_equal (key, "screen-magnifier-enabled") == FALSE)
		return;

	g_debug ("screen reader, OSK or magnifier enablement changed");

	screen_reader = g_settings_get_boolean (manager->a11y_apps_settings, "screen-reader-enabled");
	keyboard = g_settings_get_boolean (manager->a11y_apps_settings, "screen-keyboard-enabled");
	magnifier = g_settings_get_boolean (manager->a11y_apps_settings, "screen-magnifier-enabled");

	if (screen_reader || keyboard || magnifier) {
		g_debug ("Enabling toolkit-accessibility, screen reader, OSK or magnifier enabled");
		g_settings_set_boolean (manager->interface_settings, "toolkit-accessibility", TRUE);
	} else if (screen_reader == FALSE && keyboard == FALSE && magnifier == FALSE) {
		g_debug ("Disabling toolkit-accessibility, screen reader, OSK and magnifier disabled");
		g_settings_set_boolean (manager->interface_settings, "toolkit-accessibility", FALSE);
	}
}

static void
gsd_a11y_settings_manager_startup (GApplication *app)
{
        GsdA11ySettingsManager *manager = GSD_A11Y_SETTINGS_MANAGER (app);

        g_debug ("Starting a11y_settings manager");
        gnome_settings_profile_start (NULL);

	manager->interface_settings = g_settings_new ("org.gnome.desktop.interface");
	manager->a11y_apps_settings = g_settings_new ("org.gnome.desktop.a11y.applications");

	g_signal_connect (G_OBJECT (manager->a11y_apps_settings), "changed",
			  G_CALLBACK (apps_settings_changed), manager);

	/* If any of the screen reader, on-screen keyboard or magnifier are
	 * enabled, make sure a11y is enabled for the toolkits.
	 * We don't do the same thing for the reverse so it's possible to
	 * enable AT-SPI for the toolkits without using an a11y app */
	if (g_settings_get_boolean (manager->a11y_apps_settings, "screen-keyboard-enabled") ||
	    g_settings_get_boolean (manager->a11y_apps_settings, "screen-reader-enabled") ||
	    g_settings_get_boolean (manager->a11y_apps_settings, "screen-magnifier-enabled"))
		g_settings_set_boolean (manager->interface_settings, "toolkit-accessibility", TRUE);

        G_APPLICATION_CLASS (gsd_a11y_settings_manager_parent_class)->startup (app);

        gnome_settings_profile_end (NULL);
}

static void
gsd_a11y_settings_manager_shutdown (GApplication *app)
{
	GsdA11ySettingsManager *manager = GSD_A11Y_SETTINGS_MANAGER (app);

	if (manager->interface_settings) {
		g_object_unref (manager->interface_settings);
		manager->interface_settings = NULL;
	}
	if (manager->a11y_apps_settings) {
		g_object_unref (manager->a11y_apps_settings);
		manager->a11y_apps_settings = NULL;
	}

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
