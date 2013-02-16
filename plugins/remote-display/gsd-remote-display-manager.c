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
#include <gdk/gdkx.h>

#include "gnome-settings-session.h"
#include "gnome-settings-profile.h"
#include "gsd-remote-display-manager.h"

#define GSD_REMOTE_DISPLAY_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_REMOTE_DISPLAY_MANAGER, GsdRemoteDisplayManagerPrivate))

struct GsdRemoteDisplayManagerPrivate
{
        GSettings    *desktop_settings;
        GDBusProxy   *vino_proxy;
        GCancellable *cancellable;
        guint         vino_watch_id;
        gboolean      vnc_in_use;
};

static void     gsd_remote_display_manager_class_init  (GsdRemoteDisplayManagerClass *klass);
static void     gsd_remote_display_manager_init        (GsdRemoteDisplayManager      *remote_display_manager);

G_DEFINE_TYPE (GsdRemoteDisplayManager, gsd_remote_display_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static void
update_settings_from_variant (GsdRemoteDisplayManager *manager,
			      GVariant                *variant)
{
	manager->priv->vnc_in_use = g_variant_get_boolean (variant);

	g_debug ("%s because of remote display status (vnc: %d)",
		 !manager->priv->vnc_in_use ? "Enabling" : "Disabling",
		 manager->priv->vnc_in_use);
	g_settings_set_boolean (manager->priv->desktop_settings,
				"enable-animations",
				!manager->priv->vnc_in_use);
}

static void
props_changed (GDBusProxy              *proxy,
	       GVariant                *changed_properties,
	       GStrv                    invalidated_properties,
	       GsdRemoteDisplayManager *manager)
{
        GVariant *v;

        v = g_variant_lookup_value (changed_properties, "Connected", G_VARIANT_TYPE_BOOLEAN);
        if (v) {
                g_debug ("Received connected change");
                update_settings_from_variant (manager, v);
                g_variant_unref (v);
        }
}

static void
got_vino_proxy (GObject                 *source_object,
		GAsyncResult            *res,
		GsdRemoteDisplayManager *manager)
{
	GError *error = NULL;
	GVariant *v;

	manager->priv->vino_proxy = g_dbus_proxy_new_finish (res, &error);
	if (manager->priv->vino_proxy == NULL) {
		g_warning ("Failed to get Vino's D-Bus proxy: %s", error->message);
		g_error_free (error);
		return;
	}

	g_signal_connect (manager->priv->vino_proxy, "g-properties-changed",
			  G_CALLBACK (props_changed), manager);

	v = g_dbus_proxy_get_cached_property (manager->priv->vino_proxy, "Connected");
	if (v) {
                g_debug ("Setting original state");
		update_settings_from_variant (manager, v);
                g_variant_unref (v);
        }
}

static void
vino_appeared_cb (GDBusConnection         *connection,
		  const gchar             *name,
		  const gchar             *name_owner,
		  GsdRemoteDisplayManager *manager)
{
	g_debug ("Vino appeared");
	g_dbus_proxy_new (connection,
			  G_DBUS_PROXY_FLAGS_NONE,
			  NULL,
			  name,
			  "/org/gnome/vino/screens/0",
			  "org.gnome.VinoScreen",
			  manager->priv->cancellable,
			  (GAsyncReadyCallback) got_vino_proxy,
			  manager);
}

static void
vino_vanished_cb (GDBusConnection         *connection,
		  const char              *name,
		  GsdRemoteDisplayManager *manager)
{
	g_debug ("Vino vanished");
	if (manager->priv->cancellable != NULL) {
		g_cancellable_cancel (manager->priv->cancellable);
		g_clear_object (&manager->priv->cancellable);
	}
	g_clear_object (&manager->priv->vino_proxy);

	/* And reset for us to have animations */
	g_settings_set_boolean (manager->priv->desktop_settings,
				"enable-animations",
				TRUE);
}

static gboolean
gsd_display_has_extension (const gchar *ext)
{
	int op, event, error;

	return XQueryExtension (gdk_x11_get_default_xdisplay (),
				ext, &op, &event, &error);
}

gboolean
gsd_remote_display_manager_start (GsdRemoteDisplayManager *manager,
				  GError               **error)
{
        g_debug ("Starting remote-display manager");

        gnome_settings_profile_start (NULL);

        manager->priv->desktop_settings = g_settings_new ("org.gnome.desktop.interface");

	/* Check if spice is used:
	 * https://bugzilla.gnome.org/show_bug.cgi?id=680195#c7
	 * This doesn't change at run-time, so it's to the point */
	if (g_file_test ("/dev/virtio-ports/com.redhat.spice.0", G_FILE_TEST_EXISTS)) {
		g_debug ("Disabling animations because SPICE is in use");
		g_settings_set_boolean (manager->priv->desktop_settings,
					"enable-animations",
					FALSE);
		goto out;
	}

	/* Xvnc exposes an extension named VNC-EXTENSION */
	if (gsd_display_has_extension ("VNC-EXTENSION")) {
		g_debug ("Disabling animations because VNC-EXTENSION was detected");
		g_settings_set_boolean (manager->priv->desktop_settings,
					"enable-animations",
					FALSE);
		goto out;
	}

	/* Monitor Vino's usage */
	manager->priv->vino_watch_id = g_bus_watch_name (G_BUS_TYPE_SESSION,
							 "org.gnome.Vino",
							 G_BUS_NAME_WATCHER_FLAGS_NONE,
							 (GBusNameAppearedCallback) vino_appeared_cb,
							 (GBusNameVanishedCallback) vino_vanished_cb,
							 manager, NULL);

out:
        gnome_settings_profile_end (NULL);
        return TRUE;
}

void
gsd_remote_display_manager_stop (GsdRemoteDisplayManager *manager)
{
        g_debug ("Stopping remote_display manager");

	if (manager->priv->cancellable != NULL) {
		g_cancellable_cancel (manager->priv->cancellable);
		g_clear_object (&manager->priv->cancellable);
	}
	g_clear_object (&manager->priv->vino_proxy);

	if (manager->priv->desktop_settings) {
		g_settings_reset (manager->priv->desktop_settings, "enable-animations");
		g_clear_object (&manager->priv->desktop_settings);
	}
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
