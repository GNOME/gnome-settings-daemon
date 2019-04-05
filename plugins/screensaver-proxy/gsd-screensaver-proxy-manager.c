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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <locale.h>

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

#include "gnome-settings-bus.h"
#include "gnome-settings-profile.h"
#include "gsd-screensaver-proxy-manager.h"

/* As available in:
 * https://projects.kde.org/projects/kde/kde-workspace/repository/revisions/master/entry/ksmserver/screenlocker/dbus/org.freedesktop.ScreenSaver.xml
 * and documented in:
 * https://projects.kde.org/projects/kde/kde-workspace/repository/revisions/master/entry/ksmserver/screenlocker/interface.h */
static const gchar introspection_xml[] =
"<node name='/org/freedesktop/ScreenSaver'>"
    "<interface name='org.freedesktop.ScreenSaver'>"
    "<method name='Lock'/>"
    "<method name='SimulateUserActivity'/>"
    "<method name='GetActive'>"
      "<arg type='b' direction='out'/>"
    "</method>"
    "<method name='GetActiveTime'>"
      "<arg name='seconds' type='u' direction='out'/>"
    "</method>"
    "<method name='GetSessionIdleTime'>"
      "<arg name='seconds' type='u' direction='out'/>"
    "</method>"
    "<method name='SetActive'>"
      "<arg type='b' direction='out'/>"
      "<arg name='e' type='b' direction='in'/>"
    "</method>"
    "<method name='Inhibit'>"
      "<arg name='application_name' type='s' direction='in'/>"
      "<arg name='reason_for_inhibit' type='s' direction='in'/>"
      "<arg name='cookie' type='u' direction='out'/>"
    "</method>"
    "<method name='UnInhibit'>"
      "<arg name='cookie' type='u' direction='in'/>"
    "</method>"
    "<method name='Throttle'>"
      "<arg name='application_name' type='s' direction='in'/>"
      "<arg name='reason_for_inhibit' type='s' direction='in'/>"
      "<arg name='cookie' type='u' direction='out'/>"
    "</method>"
    "<method name='UnThrottle'>"
      "<arg name='cookie' type='u' direction='in'/>"
    "</method>"

    "<signal name='ActiveChanged'>"
      "<arg type='b'/>"
    "</signal>"
  "</interface>"
"</node>";
static const gchar introspection_xml2[] =
"<node name='/ScreenSaver'>"
    "<interface name='org.freedesktop.ScreenSaver'>"
    "<method name='Lock'/>"
    "<method name='SimulateUserActivity'/>"
    "<method name='GetActive'>"
      "<arg type='b' direction='out'/>"
    "</method>"
    "<method name='GetActiveTime'>"
      "<arg name='seconds' type='u' direction='out'/>"
    "</method>"
    "<method name='GetSessionIdleTime'>"
      "<arg name='seconds' type='u' direction='out'/>"
    "</method>"
    "<method name='SetActive'>"
      "<arg type='b' direction='out'/>"
      "<arg name='e' type='b' direction='in'/>"
    "</method>"
    "<method name='Inhibit'>"
      "<arg name='application_name' type='s' direction='in'/>"
      "<arg name='reason_for_inhibit' type='s' direction='in'/>"
      "<arg name='cookie' type='u' direction='out'/>"
    "</method>"
    "<method name='UnInhibit'>"
      "<arg name='cookie' type='u' direction='in'/>"
    "</method>"
    "<method name='Throttle'>"
      "<arg name='application_name' type='s' direction='in'/>"
      "<arg name='reason_for_inhibit' type='s' direction='in'/>"
      "<arg name='cookie' type='u' direction='out'/>"
    "</method>"
    "<method name='UnThrottle'>"
      "<arg name='cookie' type='u' direction='in'/>"
    "</method>"

    "<signal name='ActiveChanged'>"
      "<arg type='b'/>"
    "</signal>"
  "</interface>"
"</node>";

#define GSD_SCREENSAVER_PROXY_DBUS_SERVICE      "org.freedesktop.ScreenSaver"
#define GSD_SCREENSAVER_PROXY_DBUS_PATH         "/org/freedesktop/ScreenSaver"
#define GSD_SCREENSAVER_PROXY_DBUS_PATH2        "/ScreenSaver"
#define GSD_SCREENSAVER_PROXY_DBUS_INTERFACE    "org.freedesktop.ScreenSaver"

#define GSM_INHIBITOR_FLAG_IDLE 1 << 3

struct _GsdScreensaverProxyManager
{
        GObject                  parent;

        GsdSessionManager       *session;
        GDBusConnection         *connection;
        GCancellable            *bus_cancellable;
        GDBusNodeInfo           *introspection_data;
        GDBusNodeInfo           *introspection_data2;
        guint                    name_id;

        GHashTable              *watch_ht;  /* key = sender, value = name watch id */
        GHashTable              *cookie_ht; /* key = cookie, value = sender */
};

static void     gsd_screensaver_proxy_manager_class_init  (GsdScreensaverProxyManagerClass *klass);
static void     gsd_screensaver_proxy_manager_init        (GsdScreensaverProxyManager      *screensaver_proxy_manager);
static void     gsd_screensaver_proxy_manager_finalize    (GObject             *object);

G_DEFINE_TYPE (GsdScreensaverProxyManager, gsd_screensaver_proxy_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static void
name_vanished_cb (GDBusConnection            *connection,
                  const gchar                *name,
                  GsdScreensaverProxyManager *manager)
{
        GHashTableIter iter;
        gpointer cookie_ptr;
        const char *sender;

        /* Look for all the cookies under that name,
         * and call uninhibit for them */
        g_hash_table_iter_init (&iter, manager->cookie_ht);
        while (g_hash_table_iter_next (&iter, &cookie_ptr, (gpointer *) &sender)) {
                if (g_strcmp0 (sender, name) == 0) {
                        guint cookie = GPOINTER_TO_UINT (cookie_ptr);

                        g_dbus_proxy_call_sync (G_DBUS_PROXY (manager->session),
                                                "Uninhibit",
                                                g_variant_new ("(u)", cookie),
                                                G_DBUS_CALL_FLAGS_NONE,
                                                -1, NULL, NULL);
                        g_debug ("Removing cookie %u for sender %s",
                                 cookie, sender);
                        g_hash_table_iter_remove (&iter);
                }
        }

        g_hash_table_remove (manager->watch_ht, name);
}

/* Based on xdp_get_app_id_from_pid() from xdg-desktop-portal
 * Returns NULL on failure, keyfile with name "" if not sandboxed, and full app-info otherwise */
static GKeyFile *
parse_app_info_from_fileinfo (int pid)
{
  g_autofree char *root_path = NULL;
  g_autofree char *path = NULL;
  g_autofree char *content = NULL;
  g_autofree char *app_id = NULL;
  int root_fd = -1;
  int info_fd = -1;
  struct stat stat_buf;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GMappedFile) mapped = NULL;
  g_autoptr(GKeyFile) metadata = NULL;

  root_path = g_strdup_printf ("/proc/%u/root", pid);
  root_fd = openat (AT_FDCWD, root_path, O_RDONLY | O_NONBLOCK | O_DIRECTORY | O_CLOEXEC | O_NOCTTY);
  if (root_fd == -1)
    {
      /* Not able to open the root dir shouldn't happen. Probably the app died and
         we're failing due to /proc/$pid not existing. In that case fail instead
         of treating this as privileged. */
      return NULL;
    }

  metadata = g_key_file_new ();

  info_fd = openat (root_fd, ".flatpak-info", O_RDONLY | O_CLOEXEC | O_NOCTTY);
  close (root_fd);
  if (info_fd == -1)
    {
      if (errno == ENOENT)
        {
          /* No file => on the host */
          g_key_file_set_string (metadata, "Application", "name", "");
          return g_steal_pointer (&metadata);
        }

      /* Some weird error => failure */
      return NULL;
    }

  if (fstat (info_fd, &stat_buf) != 0 || !S_ISREG (stat_buf.st_mode))
    {
      /* Some weird fd => failure */
      close (info_fd);
      return NULL;
    }

  mapped = g_mapped_file_new_from_fd  (info_fd, FALSE, &local_error);
  if (mapped == NULL)
    {
      close (info_fd);
      return NULL;
    }

  if (!g_key_file_load_from_data (metadata,
                                  g_mapped_file_get_contents (mapped),
                                  g_mapped_file_get_length (mapped),
                                  G_KEY_FILE_NONE, &local_error))
    {
      close (info_fd);
      return NULL;
    }

  return g_steal_pointer (&metadata);
}

static char *
get_xdg_id (guint32 pid)
{
  g_autoptr(GKeyFile) app_info = NULL;

  app_info = parse_app_info_from_fileinfo (pid);
  if (app_info == NULL)
    return NULL;

  return g_key_file_get_string (app_info, "Application", "name", NULL);
}

static char *
get_app_id_from_flatpak (GDBusConnection *connection,
                         GCancellable    *cancellable,
                         const char      *sender)
{
        GVariant *ret;
        guint32 pid;
        GError *error = NULL;

        ret = g_dbus_connection_call_sync (connection,
                                           "org.freedesktop.DBus",
                                           "/org/freedesktop/DBus",
                                           "org.freedesktop.DBus",
                                           "GetConnectionUnixProcessID",
                                           g_variant_new ("(s)", sender),
                                           G_VARIANT_TYPE ("(u)"),
                                           G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                           -1,
                                           cancellable,
                                           NULL);
        if (ret == NULL) {
                g_debug ("Could not get PID for sender '%s': %s", sender, error->message);
                g_error_free (error);
                return NULL;
        }

        g_assert (g_variant_n_children (ret) > 0);
        g_variant_get_child (ret, 0, "u", &pid);
        g_variant_unref (ret);

        return get_xdg_id (pid);
}

static void
get_inhibit_args (GDBusConnection  *connection,
                  GCancellable     *cancellable,
                  GVariant         *parameters,
                  const char       *sender,
                  char            **app_id,
                  char            **reason)
{
        const char *p_app_id;
        const char *p_reason;

        g_variant_get (parameters,
                       "(ss)", &p_app_id, &p_reason);

        /* Try to get useful app id and reason for SDL games:
         * http://hg.libsdl.org/SDL/file/bc2aba33ae1f/src/core/linux/SDL_dbus.c#l315 */

        if (g_strcmp0 (p_app_id, "My SDL application") != 0) {
                *app_id = g_strdup (p_app_id);
        } else {
                *app_id = get_app_id_from_flatpak (connection, cancellable, sender);
                if (*app_id == NULL)
                        *app_id = g_strdup (p_app_id);
        }

        if (g_strcmp0 (p_reason, "Playing a game") != 0 ||
            g_strcmp0 (*app_id, "My SDL application") == 0) {
                *reason = g_strdup (p_reason);
        } else {
                GDesktopAppInfo *app_info;

                app_info = g_desktop_app_info_new (*app_id);
                if (app_info == NULL) {
                        *reason = g_strdup (p_reason);
                } else {
                        /* Translators: This is the reason to disable the screensaver, for example:
                         * Playing “Another World: 20th anniversary Edition” */
                        *reason = g_strdup_printf (_("Playing “%s”"),
                                                   g_app_info_get_name (G_APP_INFO (app_info)));
                        g_object_unref (app_info);
                }
        }
}

static void
handle_method_call (GDBusConnection       *connection,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *interface_name,
                    const gchar           *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               user_data)
{
        GsdScreensaverProxyManager *manager = GSD_SCREENSAVER_PROXY_MANAGER (user_data);

        /* Check session pointer as a proxy for whether the manager is in the
           start or stop state */
        if (manager->session == NULL) {
                return;
        }

        g_debug ("Calling method '%s.%s' for ScreenSaver Proxy",
                 interface_name, method_name);

        if (g_strcmp0 (method_name, "Inhibit") == 0) {
                GVariant *ret;
                char *app_id;
                char *reason;
                guint cookie;

                get_inhibit_args (connection,
                                  manager->priv->bus_cancellable,
                                  parameters,
                                  sender,
                                  &app_id,
                                  &reason);

                ret = g_dbus_proxy_call_sync (G_DBUS_PROXY (G_DBUS_PROXY (manager->session)),
                                              "Inhibit",
                                              g_variant_new ("(susu)",
                                                             app_id, 0, reason, GSM_INHIBITOR_FLAG_IDLE),
                                              G_DBUS_CALL_FLAGS_NONE,
                                              -1, NULL, NULL);
                g_free (reason);
                g_free (app_id);
                g_variant_get (ret, "(u)", &cookie);
                g_hash_table_insert (manager->cookie_ht,
                                     GUINT_TO_POINTER (cookie),
                                     g_strdup (sender));
                if (g_hash_table_lookup (manager->watch_ht, sender) == NULL) {
                        guint watch_id;

                        watch_id = g_bus_watch_name_on_connection (manager->connection,
                                                                   sender,
                                                                   G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                                   NULL,
                                                                   (GBusNameVanishedCallback) name_vanished_cb,
                                                                   manager,
                                                                   NULL);
                        g_hash_table_insert (manager->watch_ht,
                                             g_strdup (sender),
                                             GUINT_TO_POINTER (watch_id));
                }
                g_dbus_method_invocation_return_value (invocation, ret);
        } else if (g_strcmp0 (method_name, "UnInhibit") == 0) {
                guint cookie;

                g_variant_get (parameters, "(u)", &cookie);
                g_dbus_proxy_call_sync (G_DBUS_PROXY (manager->session),
                                        "Uninhibit",
                                        parameters,
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1, NULL, NULL);
                g_debug ("Removing cookie %u from the list for %s", cookie, sender);
                g_hash_table_remove (manager->cookie_ht, GUINT_TO_POINTER (cookie));
                g_dbus_method_invocation_return_value (invocation, NULL);
        } else if (g_strcmp0 (method_name, "Throttle") == 0) {
                g_dbus_method_invocation_return_value (invocation, NULL);
        } else if (g_strcmp0 (method_name, "UnThrottle") == 0) {
                g_dbus_method_invocation_return_value (invocation, NULL);
        } else if (g_strcmp0 (method_name, "Lock") == 0) {
                goto unimplemented;
        } else if (g_strcmp0 (method_name, "SimulateUserActivity") == 0) {
                goto unimplemented;
        } else if (g_strcmp0 (method_name, "GetActive") == 0) {
                goto unimplemented;
        } else if (g_strcmp0 (method_name, "GetActiveTime") == 0) {
                goto unimplemented;
        } else if (g_strcmp0 (method_name, "GetSessionIdleTime") == 0) {
                goto unimplemented;
        } else if (g_strcmp0 (method_name, "SetActive") == 0) {
                goto unimplemented;
        }

        return;

unimplemented:
        g_dbus_method_invocation_return_dbus_error (invocation,
                                                    "org.freedesktop.DBus.Error.NotSupported",
                                                    "This method is not implemented");
}

static const GDBusInterfaceVTable interface_vtable =
{
        handle_method_call,
        NULL, /* GetProperty */
        NULL, /* SetProperty */
};

static void
on_bus_gotten (GObject                    *source_object,
               GAsyncResult               *res,
               GsdScreensaverProxyManager *manager)
{
        GDBusConnection *connection;
        GDBusInterfaceInfo **infos;
        GError *error = NULL;

        if (manager->bus_cancellable == NULL ||
            g_cancellable_is_cancelled (manager->bus_cancellable)) {
                g_warning ("Operation has been cancelled, so not retrieving session bus");
                return;
        }

        connection = g_bus_get_finish (res, &error);
        if (connection == NULL) {
                g_warning ("Could not get session bus: %s", error->message);
                g_error_free (error);
                return;
        }
        manager->connection = connection;
        infos = manager->introspection_data->interfaces;
        g_dbus_connection_register_object (connection,
                                           GSD_SCREENSAVER_PROXY_DBUS_PATH,
                                           infos[0],
                                           &interface_vtable,
                                           manager,
                                           NULL,
                                           NULL);
        infos = manager->introspection_data2->interfaces;
        g_dbus_connection_register_object (connection,
                                           GSD_SCREENSAVER_PROXY_DBUS_PATH2,
                                           infos[0],
                                           &interface_vtable,
                                           manager,
                                           NULL,
                                           NULL);

        manager->name_id = g_bus_own_name_on_connection (manager->connection,
                                                               GSD_SCREENSAVER_PROXY_DBUS_SERVICE,
                                                               G_BUS_NAME_OWNER_FLAGS_NONE,
                                                               NULL,
                                                               NULL,
                                                               NULL,
                                                               NULL);
}

static void
register_manager_dbus (GsdScreensaverProxyManager *manager)
{
        manager->introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
        manager->introspection_data2 = g_dbus_node_info_new_for_xml (introspection_xml2, NULL);
        manager->bus_cancellable = g_cancellable_new ();
        g_assert (manager->introspection_data != NULL);
        g_assert (manager->introspection_data2 != NULL);

        g_bus_get (G_BUS_TYPE_SESSION,
                   manager->bus_cancellable,
                   (GAsyncReadyCallback) on_bus_gotten,
                   manager);
}

gboolean
gsd_screensaver_proxy_manager_start (GsdScreensaverProxyManager *manager,
                                     GError               **error)
{
        g_debug ("Starting screensaver-proxy manager");
        gnome_settings_profile_start (NULL);
        manager->session =
                gnome_settings_bus_get_session_proxy ();
        manager->watch_ht = g_hash_table_new_full (g_str_hash,
                                                         g_str_equal,
                                                         (GDestroyNotify) g_free,
                                                         (GDestroyNotify) g_bus_unwatch_name);
        manager->cookie_ht = g_hash_table_new_full (g_direct_hash,
                                                          g_direct_equal,
                                                          NULL,
                                                          (GDestroyNotify) g_free);
        gnome_settings_profile_end (NULL);
        return TRUE;
}

void
gsd_screensaver_proxy_manager_stop (GsdScreensaverProxyManager *manager)
{
        g_debug ("Stopping screensaver_proxy manager");
        g_clear_object (&manager->session);
        g_clear_pointer (&manager->watch_ht, g_hash_table_destroy);
        g_clear_pointer (&manager->cookie_ht, g_hash_table_destroy);
}

static void
gsd_screensaver_proxy_manager_class_init (GsdScreensaverProxyManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_screensaver_proxy_manager_finalize;
}

static void
gsd_screensaver_proxy_manager_init (GsdScreensaverProxyManager *manager)
{
}

static void
gsd_screensaver_proxy_manager_finalize (GObject *object)
{
        GsdScreensaverProxyManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_SCREENSAVER_PROXY_MANAGER (object));

        manager = GSD_SCREENSAVER_PROXY_MANAGER (object);

        g_return_if_fail (manager != NULL);

        gsd_screensaver_proxy_manager_stop (manager);

        if (manager->name_id != 0) {
                g_bus_unown_name (manager->name_id);
                manager->name_id = 0;
        }
        g_clear_object (&manager->connection);
        g_clear_object (&manager->bus_cancellable);
        g_clear_pointer (&manager->introspection_data, g_dbus_node_info_unref);
        g_clear_pointer (&manager->introspection_data2, g_dbus_node_info_unref);

        G_OBJECT_CLASS (gsd_screensaver_proxy_manager_parent_class)->finalize (object);
}

GsdScreensaverProxyManager *
gsd_screensaver_proxy_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_SCREENSAVER_PROXY_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
                register_manager_dbus (manager_object);
        }

        return GSD_SCREENSAVER_PROXY_MANAGER (manager_object);
}
