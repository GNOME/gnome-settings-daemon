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
#include <systemd/sd-login.h>

#include "gnome-settings-session.h"

typedef struct
{
  GSource source;
  GPollFD pollfd;
  sd_login_monitor *monitor;
} SdSource;

static gboolean
sd_source_prepare (GSource *source,
                   gint    *timeout)
{
  *timeout = -1;
  return FALSE;
}

static gboolean
sd_source_check (GSource *source)
{
  SdSource *sd_source = (SdSource *)source;

  return sd_source->pollfd.revents != 0;
}

static gboolean
sd_source_dispatch (GSource     *source,
                    GSourceFunc  callback,
                    gpointer     user_data)

{
  SdSource *sd_source = (SdSource *)source;
  gboolean ret;

  g_warn_if_fail (callback != NULL);

  ret = (*callback) (user_data);

  sd_login_monitor_flush (sd_source->monitor);
  return ret;
}

static void
sd_source_finalize (GSource *source)
{
  SdSource *sd_source = (SdSource*)source;

  sd_login_monitor_unref (sd_source->monitor);
}

static GSourceFuncs sd_source_funcs = {
  sd_source_prepare,
  sd_source_check,
  sd_source_dispatch,
  sd_source_finalize
};

static GSource *
sd_source_new (void)
{
  GSource *source;
  SdSource *sd_source;
  int ret;

  source = g_source_new (&sd_source_funcs, sizeof (SdSource));
  sd_source = (SdSource *)source;

  if ((ret = sd_login_monitor_new (NULL, &sd_source->monitor)) < 0)
    {
      g_printerr ("Error getting login monitor: %d", ret);
    }
  else
    {
      sd_source->pollfd.fd = sd_login_monitor_get_fd (sd_source->monitor);
      sd_source->pollfd.events = G_IO_IN;
      g_source_add_poll (source, &sd_source->pollfd);
    }

  return source;
}

static void gnome_settings_session_finalize (GObject *object);

#define GNOME_SETTINGS_SESSION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GNOME_TYPE_SETTINGS_SESSION, GnomeSettingsSessionPrivate))

struct GnomeSettingsSessionPrivate
{
        GSource                   *sd_source;
	gchar                     *session_id;
	GnomeSettingsSessionState  state;
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
        GnomeSettingsSessionState  state;

        state = active ? GNOME_SETTINGS_SESSION_STATE_ACTIVE
                       : GNOME_SETTINGS_SESSION_STATE_INACTIVE;
        if (session->priv->state != state) {
                session->priv->state = state;
                g_object_notify (G_OBJECT (session), "state");
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

static gboolean
sessions_changed (gpointer user_data)
{
        GnomeSettingsSession *session = user_data;
        gboolean active;

        active = sd_session_is_active (session->priv->session_id);
        gnome_settings_session_set_state (session, active);

        return TRUE;
}

static void
gnome_settings_session_init (GnomeSettingsSession *session)
{
	session->priv = GNOME_SETTINGS_SESSION_GET_PRIVATE (session);

        sd_pid_get_session (getpid(), &session->priv->session_id);

        session->priv->sd_source = sd_source_new ();
        g_source_set_callback (session->priv->sd_source, sessions_changed, session, NULL);
        g_source_attach (session->priv->sd_source, NULL);

        sessions_changed (session);
}

static void
gnome_settings_session_finalize (GObject *object)
{
	GnomeSettingsSession *session;

	session = GNOME_SETTINGS_SESSION (object);

        g_free (session->priv->session_id);

        if (session->priv->sd_source != NULL) {
                g_source_destroy (session->priv->sd_source);
                g_source_unref (session->priv->sd_source);
        }

	G_OBJECT_CLASS (gnome_settings_session_parent_class)->finalize (object);
}

static GnomeSettingsSession *session;

GnomeSettingsSession *
gnome_settings_session_new (void)
{
        if (session != NULL) {
                g_object_ref (session);
        } else {
                session = g_object_new (GNOME_TYPE_SETTINGS_SESSION, NULL);
                g_object_add_weak_pointer ((GObject*)session, (gpointer*)&session);
        }

	return session;
}

#define GNOME_SESSION_DBUS_NAME      "org.gnome.SessionManager"
#define GNOME_SESSION_DBUS_OBJECT    "/org/gnome/SessionManager"
#define GNOME_SESSION_DBUS_INTERFACE "org.gnome.SessionManager"

GDBusProxy *
gnome_settings_session_get_session_proxy (void)
{
        static GDBusProxy *session_proxy;
        GError *error =  NULL;

        if (session_proxy != NULL) {
                g_object_ref (session_proxy);
        } else {
                session_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                               G_DBUS_PROXY_FLAGS_NONE,
                                                               NULL,
                                                               GNOME_SESSION_DBUS_NAME,
                                                               GNOME_SESSION_DBUS_OBJECT,
                                                               GNOME_SESSION_DBUS_INTERFACE,
                                                               NULL,
                                                               &error);
                if (error) {
                        g_warning ("Failed to connect to the session manager: %s", error->message);
                        g_error_free (error);
                } else {
                        g_object_add_weak_pointer (G_OBJECT (session_proxy), (gpointer*)&session_proxy);
                }
        }

        return session_proxy;
}
