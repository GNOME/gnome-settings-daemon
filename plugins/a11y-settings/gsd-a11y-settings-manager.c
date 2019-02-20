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
        GObject    parent;

        GSettings *interface_settings;
        GSettings *a11y_apps_settings;
};

enum {
        PROP_0,
};

static void     gsd_a11y_settings_manager_class_init  (GsdA11ySettingsManagerClass *klass);
static void     gsd_a11y_settings_manager_init        (GsdA11ySettingsManager      *a11y_settings_manager);
static void     gsd_a11y_settings_manager_finalize    (GObject                     *object);

G_DEFINE_TYPE (GsdA11ySettingsManager, gsd_a11y_settings_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static void
apps_settings_changed (GSettings              *settings,
		       const char             *key,
		       GsdA11ySettingsManager *manager)
{
	gboolean screen_reader, keyboard;

	if (g_str_equal (key, "screen-reader-enabled") == FALSE &&
	    g_str_equal (key, "screen-keyboard-enabled") == FALSE)
		return;

	g_debug ("screen reader or OSK enablement changed");

	screen_reader = g_settings_get_boolean (manager->a11y_apps_settings, "screen-reader-enabled");
	keyboard = g_settings_get_boolean (manager->a11y_apps_settings, "screen-keyboard-enabled");

	if (screen_reader || keyboard) {
		g_debug ("Enabling toolkit-accessibility, screen reader or OSK enabled");
		g_settings_set_boolean (manager->interface_settings, "toolkit-accessibility", TRUE);
	} else if (screen_reader == FALSE && keyboard == FALSE) {
		g_debug ("Disabling toolkit-accessibility, screen reader and OSK disabled");
		g_settings_set_boolean (manager->interface_settings, "toolkit-accessibility", FALSE);
	}
}

gboolean
gsd_a11y_settings_manager_start (GsdA11ySettingsManager *manager,
                                 GError                **error)
{
        g_debug ("Starting a11y_settings manager");
        gnome_settings_profile_start (NULL);

	manager->interface_settings = g_settings_new ("org.gnome.desktop.interface");
	manager->a11y_apps_settings = g_settings_new ("org.gnome.desktop.a11y.applications");

	g_signal_connect (G_OBJECT (manager->a11y_apps_settings), "changed",
			  G_CALLBACK (apps_settings_changed), manager);

	/* If any of the screen reader or on-screen keyboard are enabled,
	 * make sure a11y is enabled for the toolkits.
	 * We don't do the same thing for the reverse so it's possible to
	 * enable AT-SPI for the toolkits without using an a11y app */
	if (g_settings_get_boolean (manager->a11y_apps_settings, "screen-keyboard-enabled") ||
	    g_settings_get_boolean (manager->a11y_apps_settings, "screen-reader-enabled"))
		g_settings_set_boolean (manager->interface_settings, "toolkit-accessibility", TRUE);

        gnome_settings_profile_end (NULL);
        return TRUE;
}

void
gsd_a11y_settings_manager_stop (GsdA11ySettingsManager *manager)
{
	if (manager->interface_settings) {
		g_object_unref (manager->interface_settings);
		manager->interface_settings = NULL;
	}
	if (manager->a11y_apps_settings) {
		g_object_unref (manager->a11y_apps_settings);
		manager->a11y_apps_settings = NULL;
	}
        g_debug ("Stopping a11y_settings manager");
}

static void
gsd_a11y_settings_manager_class_init (GsdA11ySettingsManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_a11y_settings_manager_finalize;
}

static void
gsd_a11y_settings_manager_init (GsdA11ySettingsManager *manager)
{
}

static void
gsd_a11y_settings_manager_finalize (GObject *object)
{
        GsdA11ySettingsManager *a11y_settings_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_A11Y_SETTINGS_MANAGER (object));

        a11y_settings_manager = GSD_A11Y_SETTINGS_MANAGER (object);

        gsd_a11y_settings_manager_stop (a11y_settings_manager);

        G_OBJECT_CLASS (gsd_a11y_settings_manager_parent_class)->finalize (object);
}

GsdA11ySettingsManager *
gsd_a11y_settings_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_A11Y_SETTINGS_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_A11Y_SETTINGS_MANAGER (manager_object);
}
