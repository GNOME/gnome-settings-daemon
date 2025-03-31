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
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <locale.h>

#include <glib/gi18n.h>
#include <gio/gio.h>

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
        GsdApplication           parent;

        GsdSessionManager       *session;
        GDBusNodeInfo           *introspection_data;
        GDBusNodeInfo           *introspection_data2;
        guint                    name_id;

        GHashTable              *watch_ht;  /* key = sender, value = name watch id */
        GHashTable              *cookie_ht; /* key = cookie, value = sender */
};

static void     gsd_screensaver_proxy_manager_class_init  (GsdScreensaverProxyManagerClass *klass);
static void     gsd_screensaver_proxy_manager_init        (GsdScreensaverProxyManager      *screensaver_proxy_manager);

G_DEFINE_TYPE (GsdScreensaverProxyManager, gsd_screensaver_proxy_manager, GSD_TYPE_APPLICATION)

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
        g_autoptr(GError) error = NULL;

        /* Check session pointer as a proxy for whether the manager is in the
           start or stop state */
        if (manager->session == NULL) {
                g_dbus_method_invocation_return_dbus_error (invocation,
                                                            "org.freedesktop.DBus.Error.NotSupported",
                                                            "Session is unavailable");
                return;
        }

        g_debug ("Calling method '%s.%s' for ScreenSaver Proxy",
                 interface_name, method_name);

        if (g_strcmp0 (method_name, "Inhibit") == 0) {
                GVariant *ret;
                const char *app_id;
                const char *reason;
                guint cookie;

                g_variant_get (parameters,
                               "(ss)", &app_id, &reason);

                ret = g_dbus_proxy_call_sync (G_DBUS_PROXY (G_DBUS_PROXY (manager->session)),
                                              "Inhibit",
                                              g_variant_new ("(susu)",
                                                             app_id, 0, reason, GSM_INHIBITOR_FLAG_IDLE),
                                              G_DBUS_CALL_FLAGS_NONE,
                                              -1, NULL, &error);

                if (!ret)
                        goto error;

                g_variant_get (ret, "(u)", &cookie);
                g_hash_table_insert (manager->cookie_ht,
                                     GUINT_TO_POINTER (cookie),
                                     g_strdup (sender));
                if (g_hash_table_lookup (manager->watch_ht, sender) == NULL) {
                        guint watch_id;

                        watch_id = g_bus_watch_name_on_connection (connection,
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
                GVariant *ret;
                guint cookie;

                g_variant_get (parameters, "(u)", &cookie);
                ret = g_dbus_proxy_call_sync (G_DBUS_PROXY (manager->session),
                                              "Uninhibit",
                                              parameters,
                                              G_DBUS_CALL_FLAGS_NONE,
                                              -1, NULL, &error);

                if (!ret)
                        goto error;

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
                                                    "This method is not part of the idle inhibition specification: https://specifications.freedesktop.org/idle-inhibit-spec/latest/");
        return;
error:
        g_dbus_method_invocation_return_gerror (invocation, error);
}

static const GDBusInterfaceVTable interface_vtable =
{
        handle_method_call,
        NULL, /* GetProperty */
        NULL, /* SetProperty */
};

static void
gsd_screensaver_proxy_manager_startup (GApplication *app)
{
        GsdScreensaverProxyManager *manager = GSD_SCREENSAVER_PROXY_MANAGER (app);

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

        G_APPLICATION_CLASS (gsd_screensaver_proxy_manager_parent_class)->startup (app);

        gnome_settings_profile_end (NULL);
}

static void
gsd_screensaver_proxy_manager_shutdown (GApplication *app)
{
        GsdScreensaverProxyManager *manager = GSD_SCREENSAVER_PROXY_MANAGER (app);

        g_debug ("Stopping screensaver_proxy manager");
        g_clear_object (&manager->session);
        g_clear_pointer (&manager->watch_ht, g_hash_table_destroy);
        g_clear_pointer (&manager->cookie_ht, g_hash_table_destroy);

        G_APPLICATION_CLASS (gsd_screensaver_proxy_manager_parent_class)->shutdown (app);
}

static gboolean
gsd_screensaver_proxy_manager_dbus_register (GApplication    *app,
                                             GDBusConnection *connection,
                                             const char     *object_path,
                                             GError         **error)
{
        GsdScreensaverProxyManager *manager = GSD_SCREENSAVER_PROXY_MANAGER (app);
        GDBusInterfaceInfo **infos;

        if (!G_APPLICATION_CLASS (gsd_screensaver_proxy_manager_parent_class)->dbus_register (app,
                                                                                              connection,
                                                                                              object_path,
                                                                                              error))
                return FALSE;

        manager->introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
        manager->introspection_data2 = g_dbus_node_info_new_for_xml (introspection_xml2, NULL);
        g_assert (manager->introspection_data != NULL);
        g_assert (manager->introspection_data2 != NULL);

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

        manager->name_id = g_bus_own_name_on_connection (connection,
                                                         GSD_SCREENSAVER_PROXY_DBUS_SERVICE,
                                                         G_BUS_NAME_OWNER_FLAGS_NONE,
                                                         NULL,
                                                         NULL,
                                                         NULL,
                                                         NULL);

        return TRUE;
}

static void
gsd_screensaver_proxy_manager_dbus_unregister (GApplication    *app,
                                               GDBusConnection *connection,
                                               const char      *object_path)
{
        GsdScreensaverProxyManager *manager = GSD_SCREENSAVER_PROXY_MANAGER (app);

        g_clear_pointer (&manager->introspection_data, g_dbus_node_info_unref);
        g_clear_pointer (&manager->introspection_data2, g_dbus_node_info_unref);

        g_clear_handle_id (&manager->name_id, g_bus_unown_name);

        G_APPLICATION_CLASS (gsd_screensaver_proxy_manager_parent_class)->dbus_unregister (app,
                                                                                           connection,
                                                                                           object_path);
}


static void
gsd_screensaver_proxy_manager_class_init (GsdScreensaverProxyManagerClass *klass)
{
        GApplicationClass *application_class = G_APPLICATION_CLASS (klass);

        application_class->startup = gsd_screensaver_proxy_manager_startup;
        application_class->shutdown = gsd_screensaver_proxy_manager_shutdown;
        application_class->dbus_register = gsd_screensaver_proxy_manager_dbus_register;
        application_class->dbus_unregister = gsd_screensaver_proxy_manager_dbus_unregister;
}

static void
gsd_screensaver_proxy_manager_init (GsdScreensaverProxyManager *manager)
{
}
