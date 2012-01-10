/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006-2011 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <glib.h>
#include <gio/gio.h>

#include "gnome-settings-session.h"

static void     gnome_settings_session_finalize	(GObject		*object);

#define GNOME_SETTINGS_SESSION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GNOME_TYPE_SETTINGS_SESSION, GnomeSettingsSessionPrivate))

#define CONSOLEKIT_NAME			"org.freedesktop.ConsoleKit"
#define CONSOLEKIT_PATH			"/org/freedesktop/ConsoleKit"
#define CONSOLEKIT_INTERFACE		"org.freedesktop.ConsoleKit"

#define CONSOLEKIT_MANAGER_PATH	 	"/org/freedesktop/ConsoleKit/Manager"
#define CONSOLEKIT_MANAGER_INTERFACE    "org.freedesktop.ConsoleKit.Manager"
#define CONSOLEKIT_SEAT_INTERFACE       "org.freedesktop.ConsoleKit.Seat"
#define CONSOLEKIT_SESSION_INTERFACE    "org.freedesktop.ConsoleKit.Session"

struct GnomeSettingsSessionPrivate
{
	GDBusProxy		*proxy_session;
	gchar			*session_id;
	GCancellable		*cancellable;
	GnomeSettingsSessionState state;
};

enum {
	PROP_0,
	PROP_STATE,
	PROP_LAST
};

G_DEFINE_TYPE (GnomeSettingsSession, gnome_settings_session, G_TYPE_OBJECT)

GnomeSettingsSessionState
gnome_settings_session_get_state (GnomeSettingsSession *session)
{
	g_return_val_if_fail (GNOME_IS_SETTINGS_SESSION (session),
			      GNOME_SETTINGS_SESSION_STATE_UNKNOWN);
	return session->priv->state;
}

static void
gnome_settings_session_set_state (GnomeSettingsSession *session,
				  gboolean active)
{
	session->priv->state = active ? GNOME_SETTINGS_SESSION_STATE_ACTIVE :
					GNOME_SETTINGS_SESSION_STATE_INACTIVE;
	g_object_notify (G_OBJECT (session), "state");
}

static void
gnome_settings_session_proxy_signal_cb (GDBusProxy *proxy,
					const gchar *sender_name,
					const gchar *signal_name,
					GVariant *parameters,
					GnomeSettingsSession *session)
{
	gboolean active;
	if (g_strcmp0 (signal_name, "ActiveChanged") == 0) {
		g_variant_get (parameters, "(b)", &active);
		g_debug ("emitting active: %i", active);
		gnome_settings_session_set_state (session, active);
	}
}

static void
gnome_settings_session_get_property (GObject *object,
				     guint prop_id,
				     GValue *value,
				     GParamSpec *pspec)
{
	GnomeSettingsSession *session = GNOME_SETTINGS_SESSION (object);

	switch (prop_id) {
	case PROP_STATE:
		g_value_set_enum (value, session->priv->state);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

GType
gnome_settings_session_state_get_type (void)
{
	static GType etype = 0;
	if (etype == 0) {
		static const GEnumValue values[] = {
			{ GNOME_SETTINGS_SESSION_STATE_UNKNOWN,
			  "unknown", "Unknown" },
			{ GNOME_SETTINGS_SESSION_STATE_ACTIVE,
			  "active", "Active" },
			{ GNOME_SETTINGS_SESSION_STATE_INACTIVE,
			  "inactive", "Inactive" },
			{ 0, NULL, NULL }
			};
		etype = g_enum_register_static ("GnomeSettingsSessionState", values);
	}
	return etype;
}

static void
gnome_settings_session_class_init (GnomeSettingsSessionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->get_property = gnome_settings_session_get_property;
	object_class->finalize = gnome_settings_session_finalize;
	g_type_class_add_private (klass, sizeof (GnomeSettingsSessionPrivate));

	g_object_class_install_property (object_class,
					 PROP_STATE,
					 g_param_spec_enum ("state",
							    "The session state",
							    NULL,
							    GNOME_TYPE_SETTINGS_SESSION_STATE,
							    GNOME_SETTINGS_SESSION_STATE_UNKNOWN,
							    G_PARAM_READABLE));
}

static void
is_active_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	gboolean active = FALSE;
	GError *error = NULL;
	GVariant *result;
	GnomeSettingsSession *session = GNOME_SETTINGS_SESSION (user_data);

	/* is our session active */
	result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object),
					   res,
					   &error);
	if (result == NULL) {
		g_warning ("IsActive failed: %s", error->message);
		g_error_free (error);
		return;
	}
	g_variant_get (result, "(b)", &active);
	gnome_settings_session_set_state (session, active);

	/* watch for changes */
	g_signal_connect (session->priv->proxy_session, "g-signal",
			  G_CALLBACK (gnome_settings_session_proxy_signal_cb),
			  session);

	g_variant_unref (result);
}

static void
got_session_proxy_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	GError *error = NULL;
	GnomeSettingsSession *session = GNOME_SETTINGS_SESSION (user_data);

	/* connect to session */
	session->priv->proxy_session = g_dbus_proxy_new_for_bus_finish (res,
									&error);
	if (session->priv->proxy_session == NULL) {
		g_warning ("cannot connect to %s: %s",
			   session->priv->session_id,
			   error->message);
		g_error_free (error);
		return;
	}

	/* is our session active */
	g_dbus_proxy_call (session->priv->proxy_session,
			   "IsActive",
			   NULL,
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   session->priv->cancellable,
			   is_active_cb,
			   session);
}

static void
got_session_path_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	GVariant *result;
	GError *error = NULL;
	GnomeSettingsSession *session = GNOME_SETTINGS_SESSION (user_data);

	result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object),
					   res,
					   &error);
	if (result == NULL) {
		g_warning ("Failed to get session for pid: %s",
			   error->message);
		g_error_free (error);
		return;
	}

	g_variant_get (result, "(o)", &session->priv->session_id);
	g_debug ("ConsoleKit session ID: %s", session->priv->session_id);

	/* connect to session */
	g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
				  G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
				  NULL,
				  CONSOLEKIT_NAME,
				  session->priv->session_id,
				  CONSOLEKIT_SESSION_INTERFACE,
				  session->priv->cancellable,
				  got_session_proxy_cb,
				  session);
	g_variant_unref (result);
}

static void
got_manager_proxy_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	GDBusProxy *proxy_manager;
	GError *error = NULL;
	guint32 pid;
	GnomeSettingsSession *session = GNOME_SETTINGS_SESSION (user_data);

	proxy_manager = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (proxy_manager == NULL) {
		g_warning ("cannot connect to ConsoleKit: %s",
			   error->message);
		g_error_free (error);
		return;
	}

	/* get the session we are running in */
	pid = getpid ();
	g_dbus_proxy_call (proxy_manager,
			   "GetSessionForUnixProcess",
			   g_variant_new ("(u)", pid),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1, session->priv->cancellable,
			   got_session_path_cb,
			   session);
	g_object_unref (proxy_manager);
}

static void
gnome_settings_session_init (GnomeSettingsSession *session)
{
	session->priv = GNOME_SETTINGS_SESSION_GET_PRIVATE (session);
	session->priv->cancellable = g_cancellable_new ();

	/* connect to ConsoleKit */
	g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
				  G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
				  NULL,
				  CONSOLEKIT_NAME,
				  CONSOLEKIT_MANAGER_PATH,
				  CONSOLEKIT_MANAGER_INTERFACE,
				  session->priv->cancellable,
				  got_manager_proxy_cb,
				  session);
}

static void
gnome_settings_session_finalize (GObject *object)
{
	GnomeSettingsSession *session;

	g_return_if_fail (GNOME_IS_SETTINGS_SESSION (object));

	session = GNOME_SETTINGS_SESSION (object);

	g_cancellable_cancel (session->priv->cancellable);

	g_return_if_fail (session->priv != NULL);
	if (session->priv->proxy_session != NULL)
		g_object_unref (session->priv->proxy_session);
	g_object_unref (session->priv->cancellable);
	g_free (session->priv->session_id);

	G_OBJECT_CLASS (gnome_settings_session_parent_class)->finalize (object);
}

GnomeSettingsSession *
gnome_settings_session_new (void)
{
	GnomeSettingsSession *session;
	session = g_object_new (GNOME_TYPE_SETTINGS_SESSION, NULL);
	return GNOME_SETTINGS_SESSION (session);
}

UpClient *
gnome_settings_session_get_upower_client (void)
{
	static UpClient *client;

	if (client != NULL)
		return g_object_ref (client);

	client = up_client_new ();
	g_object_add_weak_pointer (G_OBJECT (client), (gpointer *) &client);

	return client;
}
