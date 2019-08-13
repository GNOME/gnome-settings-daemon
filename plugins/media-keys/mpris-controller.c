/*
 * Copyright © 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses>
 *
 * Author: Michael Wood <michael.g.wood@intel.com>
 */

#include "mpris-controller.h"
#include "bus-watch-namespace.h"
#include <gio/gio.h>

enum {
  PROP_0,
  PROP_HAS_ACTIVE_PLAYER
};

struct _MprisController
{
  GObject parent;

  GCancellable *cancellable;
  GDBusProxy *mpris_client_proxy;
  guint namespace_watcher_id;
  GSList *other_players;
  gboolean connecting;
};

G_DEFINE_TYPE (MprisController, mpris_controller, G_TYPE_OBJECT)

static void mpris_player_try_connect (MprisController *self);

static void
mpris_controller_dispose (GObject *object)
{
  MprisController *self = MPRIS_CONTROLLER (object);

  g_clear_object (&self->cancellable);
  g_clear_object (&self->mpris_client_proxy);

  if (self->namespace_watcher_id)
    {
      bus_unwatch_namespace (self->namespace_watcher_id);
      self->namespace_watcher_id = 0;
    }

  if (self->other_players)
    {
      g_slist_free_full (self->other_players, g_free);
      self->other_players = NULL;
    }

  G_OBJECT_CLASS (mpris_controller_parent_class)->dispose (object);
}

static void
mpris_proxy_call_done (GObject      *object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  GError *error = NULL;
  GVariant *ret;

  if (!(ret = g_dbus_proxy_call_finish (G_DBUS_PROXY (object), res, &error)))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Error calling method %s", error->message);
      g_clear_error (&error);
      return;
    }
  g_variant_unref (ret);
}

gboolean
mpris_controller_key (MprisController *self, const gchar *key)
{
  if (!self->mpris_client_proxy)
    return FALSE;

  if (g_strcmp0 (key, "Play") == 0)
    key = "PlayPause";

  g_debug ("calling %s over dbus to mpris client %s",
           key, g_dbus_proxy_get_name (self->mpris_client_proxy));
  g_dbus_proxy_call (self->mpris_client_proxy,
                     key, NULL, 0, -1, self->cancellable,
                     mpris_proxy_call_done,
                     NULL);
  return TRUE;
}

static void
mpris_client_notify_name_owner_cb (GDBusProxy      *proxy,
                                   GParamSpec      *pspec,
                                   MprisController *self)
{
  g_autofree gchar *name_owner = NULL;

  /* Owner changed, but the proxy is still valid. */
  name_owner = g_dbus_proxy_get_name_owner (proxy);
  if (name_owner)
    return;

  g_clear_object (&self->mpris_client_proxy);
  g_object_notify (G_OBJECT (self), "has-active-player");

  mpris_player_try_connect (self);
}

static void
mpris_proxy_ready_cb (GObject      *object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  MprisController *self = MPRIS_CONTROLLER (user_data);
  GError *error = NULL;
  GDBusProxy *proxy;

  proxy = g_dbus_proxy_new_for_bus_finish (res, &error);

  if (!proxy)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          g_warning ("Error connecting to mpris interface %s", error->message);

          self->connecting = FALSE;

          mpris_player_try_connect (MPRIS_CONTROLLER (user_data));
        }
      g_clear_error (&error);
      return;
    }

  self->mpris_client_proxy = proxy;
  self->connecting = FALSE;

  g_signal_connect (proxy, "notify::g-name-owner",
                    G_CALLBACK (mpris_client_notify_name_owner_cb), user_data);

  g_object_notify (user_data, "has-active-player");
}

static void
start_mpris_proxy (MprisController *self, const gchar *name)
{
  g_debug ("Creating proxy for for %s", name);
  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                            0,
                            NULL,
                            name,
                            "/org/mpris/MediaPlayer2",
                            "org.mpris.MediaPlayer2.Player",
                            self->cancellable,
                            mpris_proxy_ready_cb,
                            self);
  self->connecting = TRUE;
}

static void
mpris_player_try_connect (MprisController *self)
{
  GSList *first;
  gchar *name;

  if (self->connecting || self->mpris_client_proxy)
    return;

  if (!self->other_players)
    return;

  first = self->other_players;
  name = first->data;

  start_mpris_proxy (self, name);

  self->other_players = self->other_players->next;
  g_free (name);
  g_slist_free_1 (first);
}

static void
mpris_player_appeared (GDBusConnection *connection,
                       const gchar     *name,
                       const gchar     *name_owner,
                       gpointer         user_data)
{
  MprisController *self = user_data;

  self->other_players = g_slist_prepend (self->other_players, g_strdup (name));
  mpris_player_try_connect (self);
}

static void
mpris_player_vanished (GDBusConnection *connection,
                       const gchar     *name,
                       gpointer         user_data)
{
  MprisController *self = user_data;
  GSList *elem;

  elem = g_slist_find_custom (self->other_players, name, (GCompareFunc) g_strcmp0);
  if (elem)
    {
      self->other_players = g_slist_remove_link (self->other_players, elem);
      g_free (elem->data);
      g_slist_free_1 (elem);
    }
}

static void
mpris_controller_constructed (GObject *object)
{
  MprisController *self = MPRIS_CONTROLLER (object);

  self->namespace_watcher_id = bus_watch_namespace (G_BUS_TYPE_SESSION,
                                                    "org.mpris.MediaPlayer2",
                                                    mpris_player_appeared,
                                                    mpris_player_vanished,
                                                    MPRIS_CONTROLLER (object),
                                                    NULL);
}

static void
mpris_controller_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  MprisController *self = MPRIS_CONTROLLER (object);

  switch (prop_id) {
  case PROP_HAS_ACTIVE_PLAYER:
    g_value_set_boolean (value,
                         mpris_controller_get_has_active_player (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
mpris_controller_class_init (MprisControllerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = mpris_controller_constructed;
  object_class->dispose = mpris_controller_dispose;
  object_class->get_property = mpris_controller_get_property;

  g_object_class_install_property (object_class,
                                   PROP_HAS_ACTIVE_PLAYER,
                                   g_param_spec_boolean ("has-active-player",
                                                         NULL,
                                                         NULL,
                                                         FALSE,
                                                         G_PARAM_READABLE));
}

static void
mpris_controller_init (MprisController *self)
{
}

gboolean
mpris_controller_get_has_active_player (MprisController *controller)
{
  g_return_val_if_fail (MPRIS_IS_CONTROLLER (controller), FALSE);

  return (controller->mpris_client_proxy != NULL);
}

MprisController *
mpris_controller_new (void)
{
  return g_object_new (MPRIS_TYPE_CONTROLLER, NULL);
}
