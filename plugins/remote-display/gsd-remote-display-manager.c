/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Bastien Nocera <hadess@hadess.net>
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

#include "gnome-settings-session.h"
#include "gnome-settings-profile.h"
#include "gsd-remote-display-manager.h"

#define GSD_REMOTE_DISPLAY_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_REMOTE_DISPLAY_MANAGER, GsdRemoteDisplayManagerPrivate))

struct GsdRemoteDisplayManagerPrivate
{
        GSettings *desktop_settings;
        GSettings *vnc_settings;
};

static void     gsd_remote_display_manager_class_init  (GsdRemoteDisplayManagerClass *klass);
static void     gsd_remote_display_manager_init        (GsdRemoteDisplayManager      *remote_display_manager);

G_DEFINE_TYPE (GsdRemoteDisplayManager, gsd_remote_display_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static void
vnc_settings_changed (GSettings *settings,
		      gchar     *key,
		      GsdRemoteDisplayManager *manager)
{
	gboolean state;

	if (g_strcmp0 (key, "enabled") != 0)
		return;

	state = g_settings_get_boolean (settings, key);
	g_settings_set_boolean (manager->priv->desktop_settings, "enable-animations", !state);
}

static gboolean
schema_is_installed (const gchar *name)
{
	const gchar * const *schemas;
	const gchar * const *s;

	schemas = g_settings_list_schemas ();
	for (s = schemas; *s; ++s)
		if (g_str_equal (*s, name))
			return TRUE;

	return FALSE;
}

gboolean
gsd_remote_display_manager_start (GsdRemoteDisplayManager *manager,
				  GError               **error)
{
        g_debug ("Starting remote-display manager");

        gnome_settings_profile_start (NULL);

        manager->priv->desktop_settings = g_settings_new ("org.gnome.desktop.interface");

	/* Check if vino is installed */
	if (schema_is_installed ("org.gnome.Vino")) {
		manager->priv->vnc_settings = g_settings_new ("org.gnome.Vino");
		g_signal_connect (G_OBJECT (manager->priv->vnc_settings), "changed",
				  G_CALLBACK (vnc_settings_changed), manager);
		vnc_settings_changed (manager->priv->vnc_settings, "enabled", manager);
	}

	/* FIXME:
	 * Check if session is being used in spice */

        gnome_settings_profile_end (NULL);
        return TRUE;
}

void
gsd_remote_display_manager_stop (GsdRemoteDisplayManager *manager)
{
        g_debug ("Stopping remote_display manager");
        g_clear_object (&manager->priv->vnc_settings);
        g_clear_object (&manager->priv->desktop_settings);
}

static void
gsd_remote_display_manager_class_init (GsdRemoteDisplayManagerClass *klass)
{
        g_type_class_add_private (klass, sizeof (GsdRemoteDisplayManagerPrivate));
}

static void
gsd_remote_display_manager_init (GsdRemoteDisplayManager *manager)
{
        manager->priv = GSD_REMOTE_DISPLAY_MANAGER_GET_PRIVATE (manager);
}

GsdRemoteDisplayManager *
gsd_remote_display_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_REMOTE_DISPLAY_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_REMOTE_DISPLAY_MANAGER (manager_object);
}
