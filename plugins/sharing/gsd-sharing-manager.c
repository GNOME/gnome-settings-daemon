/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Bastien Nocera <hadess@hadess.net>
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

#include <locale.h>
#include <glib.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gstdio.h>

#if HAVE_SYSTEMD_LIB
#include <systemd/sd-login.h>
#endif

#if HAVE_NETWORK_MANAGER
#include <NetworkManager.h>
#endif /* HAVE_NETWORK_MANAGER */

#include "gnome-settings-profile.h"
#include "gnome-settings-systemd.h"
#include "gsd-sharing-manager.h"
#include "gsd-sharing-enums.h"

#define SYSTEM_SERVICE_RESTART_TIMEOUT 10 /* seconds */

typedef struct {
        const char  *name;
        GSettings   *settings;
} ConfigurableServiceInfo;

typedef struct {
        const char *system_bus_name;
        const char *user_service_desktop_id;
        const char *user_service_name;
        const char *local_session_classes[3];
        const char *remote_session_classes[3];
} AssignedService;

typedef struct {
        AssignedService *service;
        guint system_bus_name_watch;
        gboolean system_service_running;
        GPid pid;
        guint child_watch_id;
        GCancellable *cancellable;
} AssignedServiceInfo;

struct _GsdSharingManager
{
        GsdApplication           parent;

        GDBusNodeInfo           *introspection_data;
        guint                    name_id;
        GDBusConnection         *connection;

        GCancellable            *cancellable;
#if HAVE_NETWORK_MANAGER
        NMClient                *client;
#endif /* HAVE_NETWORK_MANAGER */

        GHashTable              *configurable_services;
        GHashTable              *assigned_services;

        char                    *current_network;
        char                    *current_network_name;
        char                    *carrier_type;
        GsdSharingStatus         sharing_status;

        gboolean                 is_systemd_managed;
};

#define GSD_DBUS_NAME "org.gnome.SettingsDaemon"
#define GSD_DBUS_PATH "/org/gnome/SettingsDaemon"
#define GSD_DBUS_BASE_INTERFACE "org.gnome.SettingsDaemon"

#define GSD_SHARING_DBUS_NAME GSD_DBUS_NAME ".Sharing"
#define GSD_SHARING_DBUS_PATH GSD_DBUS_PATH "/Sharing"

static const gchar introspection_xml[] =
"<node>"
"  <interface name='org.gnome.SettingsDaemon.Sharing'>"
"    <annotation name='org.freedesktop.DBus.GLib.CSymbol' value='gsd_sharing_manager'/>"
"    <property name='CurrentNetwork' type='s' access='read'/>"
"    <property name='CurrentNetworkName' type='s' access='read'/>"
"    <property name='CarrierType' type='s' access='read'/>"
"    <property name='SharingStatus' type='u' access='read'/>"
"    <method name='EnableService'>"
"      <arg name='service-name' direction='in' type='s'/>"
"    </method>"
"    <method name='DisableService'>"
"      <arg name='service-name' direction='in' type='s'/>"
"      <arg name='network' direction='in' type='s'/>"
"    </method>"
"    <method name='ListNetworks'>"
"      <arg name='service-name' direction='in' type='s'/>"
"      <arg name='networks' direction='out' type='a(sss)'/>"
"    </method>"
"  </interface>"
"</node>";

static void     gsd_sharing_manager_class_init  (GsdSharingManagerClass *klass);
static void     gsd_sharing_manager_init        (GsdSharingManager      *manager);
static void     gsd_sharing_manager_finalize    (GObject                *object);

static void     gsd_sharing_manager_start_service (GsdSharingManager *manager,
                                                   const char        *service_name);
static void     gsd_sharing_manager_stop_service (GsdSharingManager *manager,
                                                  const char        *service_name);

G_DEFINE_TYPE (GsdSharingManager, gsd_sharing_manager, GSD_TYPE_APPLICATION)

static const char * const configurable_services[] = {
        "rygel",
        "gnome-user-share-webdav"
};

/* Services that are delegated to the user session by a system service
 */
static AssignedService assigned_services[] = {
        {
                .system_bus_name = "org.gnome.RemoteDesktop",
                .user_service_desktop_id = "org.gnome.RemoteDesktop.Handover.desktop",
                .user_service_name = "gnome-remote-desktop-handover",
                .local_session_classes = { NULL },
                .remote_session_classes = { "user", "greeter", NULL }
        }
};

static void
handle_unit_cb (GObject      *source_object,
                GAsyncResult *res,
                gpointer      user_data)
{
        g_autoptr (GError) error = NULL;

        gnome_settings_systemd_manage_unit_finish (G_DBUS_CONNECTION (source_object),
                                                   res, &error);

        if (error) {
                g_autofree gchar *remote_error = g_dbus_error_get_remote_error (error);

                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
                    g_strcmp0 (remote_error, "org.freedesktop.systemd1.NoSuchUnit") != 0)
                        g_warning ("%s", error->message);
        }
}

static void
gsd_sharing_manager_handle_service (GsdSharingManager *manager,
                                    const char        *service_name,
                                    gboolean           running)
{
        g_autofree char *unit = NULL;
        unit = g_strdup_printf ("%s.service", service_name);

        gnome_settings_systemd_manage_unit (manager->connection,
                                            unit,
                                            running,
                                            FALSE, /* don't enable/disable the unit */
                                            manager->cancellable,
                                            handle_unit_cb,
                                            NULL);
}

static void
gsd_sharing_manager_start_service (GsdSharingManager *manager,
                                   const char        *service_name)
{
        g_debug ("About to start %s", service_name);
        gsd_sharing_manager_handle_service (manager, service_name, TRUE);
}

static void
gsd_sharing_manager_stop_service (GsdSharingManager *manager,
                                  const char        *service_name)
{
        g_debug ("About to stop %s", service_name);
        gsd_sharing_manager_handle_service (manager, service_name, FALSE);
}

#if HAVE_NETWORK_MANAGER
static gboolean
service_is_enabled_on_current_connection (GsdSharingManager       *manager,
                                          ConfigurableServiceInfo *service)
{
        char **connections;
        int j;
        gboolean ret;
        connections = g_settings_get_strv (service->settings, "enabled-connections");
        ret = FALSE;
        for (j = 0; connections[j] != NULL; j++) {
                if (g_strcmp0 (connections[j], manager->current_network) == 0) {
                        ret = TRUE;
                        break;
                }
        }

        g_strfreev (connections);
        return ret;
}
#else
static gboolean
service_is_enabled_on_current_connection (GsdSharingManager       *manager,
                                          ConfigurableServiceInfo *service)
{
        return FALSE;
}
#endif /* HAVE_NETWORK_MANAGER */

static void
gsd_sharing_manager_sync_configurable_services (GsdSharingManager *manager)
{
        GList *services, *l;

        services = g_hash_table_get_values (manager->configurable_services);

        for (l = services; l != NULL; l = l->next) {
                ConfigurableServiceInfo *service = l->data;
                gboolean should_be_started = FALSE;

                if (manager->sharing_status == GSD_SHARING_STATUS_AVAILABLE &&
                    service_is_enabled_on_current_connection (manager, service))
                        should_be_started = TRUE;

                if (should_be_started)
                        gsd_sharing_manager_start_service (manager, service->name);
                else
                        gsd_sharing_manager_stop_service (manager, service->name);
        }
        g_list_free (services);
}


#if HAVE_SYSTEMD_LIB
static void
on_assigned_service_finished (GPid     pid,
                              int      exit_status,
                              gpointer user_data)
{
        AssignedServiceInfo *info = user_data;
        AssignedService *service = info->service;

        g_debug ("%s with pid %d exited with status %d", service->user_service_name, (int) pid, exit_status);

        info->pid = 0;
        info->child_watch_id = 0;
}

static void
on_assigned_service_started (GDesktopAppInfo *app_info,
                             GPid             pid,
                             gpointer         user_data)
{
        AssignedServiceInfo *info = user_data;
        AssignedService *service = info->service;

        g_debug ("%s started with pid %d", service->user_service_name, (int) pid);

        info->pid = pid;
        info->child_watch_id = g_child_watch_add (pid, on_assigned_service_finished, user_data);
}

static void
start_assigned_service (GsdSharingManager   *manager,
                        AssignedServiceInfo *info)
{
        AssignedService *service;

        if (manager->sharing_status == GSD_SHARING_STATUS_OFFLINE)
                return;

        if (!info->system_service_running)
                return;

        g_cancellable_cancel (info->cancellable);
        g_clear_object (&info->cancellable);

        service = info->service;

        if (manager->is_systemd_managed) {
                gsd_sharing_manager_start_service (manager, service->user_service_name);
        } else {
                g_autoptr(GDesktopAppInfo) app_info = NULL;
                g_autoptr(GError) error = NULL;
                guint spawn_flags = G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH;

#if GLIB_CHECK_VERSION(2, 74, 0)
                spawn_flags |= G_SPAWN_CHILD_INHERITS_STDERR | G_SPAWN_CHILD_INHERITS_STDOUT;
#endif

                if (info->pid != 0)
                        return;

                g_debug ("About to start %s directly", service->user_service_name);

                app_info = g_desktop_app_info_new (service->user_service_desktop_id);
                if (!g_desktop_app_info_launch_uris_as_manager (app_info,
                                                                NULL,
                                                                NULL,
                                                                spawn_flags,
                                                                NULL,
                                                                NULL,
                                                                on_assigned_service_started,
                                                                info,
                                                                &error)) {
                        g_warning ("Could not start %s: %s", service->user_service_desktop_id, error->message);
                }
        }
}

static void
stop_assigned_service (GsdSharingManager   *manager,
                       AssignedServiceInfo *info)
{
        AssignedService *service = info->service;

        if (manager->is_systemd_managed) {
                gsd_sharing_manager_stop_service (manager, service->user_service_name);
        } else {
                if (info->pid == 0)
                        return;

                g_debug ("About to stop %s directly", service->user_service_name);

                kill (info->pid, SIGTERM);
        }
}

static void
on_done_waiting_to_stop (GsdSharingManager   *manager,
                         GTask               *task,
                         AssignedServiceInfo *info)
{
        gboolean completed;

        completed = g_task_propagate_boolean (task, NULL);

        if (!completed)
                return;

        stop_assigned_service (manager, info);
}

static gboolean
on_timeout_reached (GTask *task)
{
        if (!g_task_return_error_if_cancelled (task))
                g_task_return_boolean (task, TRUE);

        return G_SOURCE_REMOVE;
}

static void
stop_assigned_service_after_timeout (GsdSharingManager   *manager,
                                     AssignedServiceInfo *info)
{
        g_autoptr (GTask) wait_task = NULL;
        g_autoptr (GSource) timeout_source = NULL;

        g_cancellable_cancel (info->cancellable);
        g_set_object (&info->cancellable, g_cancellable_new ());

        wait_task = g_task_new (manager,
                                info->cancellable,
                                (GAsyncReadyCallback)
                                on_done_waiting_to_stop,
                                info);
        timeout_source = g_timeout_source_new (SYSTEM_SERVICE_RESTART_TIMEOUT * 1000);
        g_source_set_name (timeout_source, "[gnome-settings-daemon] on_done_waiting_to_stop");

        g_task_attach_source (g_steal_pointer (&wait_task),
                              timeout_source,
                              G_SOURCE_FUNC (on_timeout_reached));
}
#endif

static void
gsd_sharing_manager_sync_assigned_services (GsdSharingManager *manager)
{
#if HAVE_SYSTEMD_LIB
        GList *services, *l;

        services = g_hash_table_get_values (manager->assigned_services);

        for (l = services; l != NULL; l = l->next) {
                AssignedServiceInfo *info = l->data;

                if (manager->sharing_status == GSD_SHARING_STATUS_OFFLINE)
                        stop_assigned_service (manager, info);
                else
                        start_assigned_service (manager, info);
        }
        g_list_free (services);
#endif
}

static void
gsd_sharing_manager_sync_services (GsdSharingManager *manager)
{
        gsd_sharing_manager_sync_configurable_services (manager);
        gsd_sharing_manager_sync_assigned_services (manager);
}

#if HAVE_NETWORK_MANAGER
static void
properties_changed (GsdSharingManager *manager)
{
        GVariantBuilder props_builder;
        GVariant *props_changed = NULL;

        /* not yet connected to the session bus */
        if (manager->connection == NULL)
                return;

        g_variant_builder_init (&props_builder, G_VARIANT_TYPE ("a{sv}"));

        g_variant_builder_add (&props_builder, "{sv}", "CurrentNetwork",
                               g_variant_new_string (manager->current_network));
        g_variant_builder_add (&props_builder, "{sv}", "CurrentNetworkName",
                               g_variant_new_string (manager->current_network_name));
        g_variant_builder_add (&props_builder, "{sv}", "CarrierType",
                               g_variant_new_string (manager->carrier_type));
        g_variant_builder_add (&props_builder, "{sv}", "SharingStatus",
                               g_variant_new_uint32 (manager->sharing_status));

        props_changed = g_variant_new ("(s@a{sv}@as)", GSD_SHARING_DBUS_NAME,
                                       g_variant_builder_end (&props_builder),
                                       g_variant_new_strv (NULL, 0));

        g_dbus_connection_emit_signal (manager->connection,
                                       NULL,
                                       GSD_SHARING_DBUS_PATH,
                                       "org.freedesktop.DBus.Properties",
                                       "PropertiesChanged",
                                       props_changed, NULL);
}

static char **
get_connections_for_service (GsdSharingManager *manager,
                             const char        *service_name)
{
        ConfigurableServiceInfo *service;

        service = g_hash_table_lookup (manager->configurable_services, service_name);
        return g_settings_get_strv (service->settings, "enabled-connections");
}
#else
static char **
get_connections_for_service (GsdSharingManager *manager,
                             const char        *service_name)
{
        const char * const * connections [] = { NULL };
        return g_strdupv ((char **) connections);
}
#endif /* HAVE_NETWORK_MANAGER */

static gboolean
check_service (GsdSharingManager  *manager,
               const char         *service_name,
               GError            **error)
{
        if (g_hash_table_lookup (manager->configurable_services, service_name))
                return TRUE;

        g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                     "Invalid service name '%s'", service_name);
        return FALSE;
}

static gboolean
gsd_sharing_manager_enable_service (GsdSharingManager  *manager,
                                    const char         *service_name,
                                    GError            **error)
{
        ConfigurableServiceInfo *service;
        char **connections;
        GPtrArray *array;
        guint i;

        if (!check_service (manager, service_name, error))
                return FALSE;

        if (manager->sharing_status != GSD_SHARING_STATUS_AVAILABLE) {
                g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                             "Sharing cannot be enabled on this network, status is '%d'", manager->sharing_status);
                return FALSE;
        }

        service = g_hash_table_lookup (manager->configurable_services, service_name);
        connections = g_settings_get_strv (service->settings, "enabled-connections");
        array = g_ptr_array_new ();
        for (i = 0; connections[i] != NULL; i++) {
                if (g_strcmp0 (connections[i], manager->current_network) == 0)
                        goto bail;
                g_ptr_array_add (array, connections[i]);
        }
        g_ptr_array_add (array, manager->current_network);
        g_ptr_array_add (array, NULL);

        g_settings_set_strv (service->settings, "enabled-connections", (const gchar *const *) array->pdata);

bail:

        gsd_sharing_manager_start_service (manager, service->name);

        g_ptr_array_unref (array);
        g_strfreev (connections);

        return TRUE;
}

static gboolean
gsd_sharing_manager_disable_service (GsdSharingManager  *manager,
                                     const char         *service_name,
                                     const char         *network_name,
                                     GError            **error)
{
        ConfigurableServiceInfo *service;
        char **connections;
        GPtrArray *array;
        guint i;

        if (!check_service (manager, service_name, error))
                return FALSE;

        service = g_hash_table_lookup (manager->configurable_services, service_name);
        connections = g_settings_get_strv (service->settings, "enabled-connections");
        array = g_ptr_array_new ();
        for (i = 0; connections[i] != NULL; i++) {
                if (g_strcmp0 (connections[i], network_name) != 0)
                        g_ptr_array_add (array, connections[i]);
        }
        g_ptr_array_add (array, NULL);

        g_settings_set_strv (service->settings, "enabled-connections", (const gchar *const *) array->pdata);
        g_ptr_array_unref (array);
        g_strfreev (connections);

        if (g_str_equal (network_name, manager->current_network))
                gsd_sharing_manager_stop_service (manager, service->name);

        return TRUE;
}

#if HAVE_NETWORK_MANAGER
static const char *
get_type_and_name_for_connection_uuid (GsdSharingManager *manager,
                                       const char        *uuid,
                                       const char       **name)
{
        NMRemoteConnection *conn;
        const char *type;

        if (!manager->client)
                return NULL;

        conn = nm_client_get_connection_by_uuid (manager->client, uuid);
        if (!conn)
                return NULL;
        type = nm_connection_get_connection_type (NM_CONNECTION (conn));
        *name = nm_connection_get_id (NM_CONNECTION (conn));

        return type;
}
#else
static const char *
get_type_and_name_for_connection_uuid (GsdSharingManager *manager,
                                       const char        *id,
                                       const char       **name)
{
        return NULL;
}
#endif /* HAVE_NETWORK_MANAGER */

#if HAVE_NETWORK_MANAGER
static gboolean
connection_is_low_security (GsdSharingManager *manager,
                            const char        *uuid)
{
        NMRemoteConnection *conn;

        if (!manager->client)
                return TRUE;

        conn = nm_client_get_connection_by_uuid (manager->client, uuid);
        if (!conn)
                return TRUE;

        /* Disable sharing on open Wi-Fi
         * XXX: Also do this for WEP networks? */
        return (nm_connection_get_setting_wireless_security (NM_CONNECTION (conn)) == NULL);
}
#endif /* HAVE_NETWORK_MANAGER */

static GVariant *
gsd_sharing_manager_list_networks (GsdSharingManager  *manager,
                                   const char         *service_name,
                                   GError            **error)
{
        char **connections;
        GVariantBuilder builder;
        guint i;

        if (!check_service (manager, service_name, error))
                return NULL;

#if HAVE_NETWORK_MANAGER
        if (!manager->client) {
                g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, "Not ready yet");
                return NULL;
        }
#endif /* HAVE_NETWORK_MANAGER */

        connections = get_connections_for_service (manager, service_name);

        g_variant_builder_init (&builder, G_VARIANT_TYPE ("(a(sss))"));
        g_variant_builder_open (&builder, G_VARIANT_TYPE ("a(sss)"));

        for (i = 0; connections[i] != NULL; i++) {
                const char *type, *name;

                type = get_type_and_name_for_connection_uuid (manager, connections[i], &name);
                if (!type)
                        continue;

                g_variant_builder_add (&builder, "(sss)", connections[i], name, type);
        }
        g_strfreev (connections);

        g_variant_builder_close (&builder);

        return g_variant_builder_end (&builder);
}

static GVariant *
handle_get_property (GDBusConnection *connection,
                     const gchar     *sender,
                     const gchar     *object_path,
                     const gchar     *interface_name,
                     const gchar     *property_name,
                     GError         **error,
                     gpointer         user_data)
{
        GsdSharingManager *manager = GSD_SHARING_MANAGER (user_data);

        /* Check session pointer as a proxy for whether the manager is in the
           start or stop state */
        if (manager->connection == NULL)
                return NULL;

        if (g_strcmp0 (property_name, "CurrentNetwork") == 0) {
                return g_variant_new_string (manager->current_network);
        }

        if (g_strcmp0 (property_name, "CurrentNetworkName") == 0) {
                return g_variant_new_string (manager->current_network_name);
        }

        if (g_strcmp0 (property_name, "CarrierType") == 0) {
                return g_variant_new_string (manager->carrier_type);
        }

        if (g_strcmp0 (property_name, "SharingStatus") == 0) {
                return g_variant_new_uint32 (manager->sharing_status);
        }

        return NULL;
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
        GsdSharingManager *manager = (GsdSharingManager *) user_data;

        g_debug ("Calling method '%s' for sharing", method_name);

        /* Check session pointer as a proxy for whether the manager is in the
           start or stop state */
        if (manager->connection == NULL)
                return;

        if (g_strcmp0 (method_name, "EnableService") == 0) {
                const char *service;
                GError *error = NULL;

                g_variant_get (parameters, "(&s)", &service);
                if (!gsd_sharing_manager_enable_service (manager, service, &error))
                        g_dbus_method_invocation_take_error (invocation, error);
                else
                        g_dbus_method_invocation_return_value (invocation, NULL);
        } else if (g_strcmp0 (method_name, "DisableService") == 0) {
                const char *service;
                const char *network_name;
                GError *error = NULL;

                g_variant_get (parameters, "(&s&s)", &service, &network_name);
                if (!gsd_sharing_manager_disable_service (manager, service, network_name, &error))
                        g_dbus_method_invocation_take_error (invocation, error);
                else
                        g_dbus_method_invocation_return_value (invocation, NULL);
        } else if (g_strcmp0 (method_name, "ListNetworks") == 0) {
                const char *service;
                GError *error = NULL;
                GVariant *variant;

                g_variant_get (parameters, "(&s)", &service);
                variant = gsd_sharing_manager_list_networks (manager, service, &error);
                if (!variant)
                        g_dbus_method_invocation_take_error (invocation, error);
                else
                        g_dbus_method_invocation_return_value (invocation, variant);
        }
}

static const GDBusInterfaceVTable interface_vtable =
{
        handle_method_call,
        handle_get_property,
        NULL
};

static void
on_bus_gotten (GObject               *source_object,
               GAsyncResult          *res,
               GsdSharingManager     *manager)
{
        GDBusConnection *connection;
        GError *error = NULL;

        connection = g_bus_get_finish (res, &error);
        if (connection == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Could not get session bus: %s", error->message);
                g_error_free (error);
                return;
        }
        manager->connection = connection;

        g_dbus_connection_register_object (connection,
                                           GSD_SHARING_DBUS_PATH,
                                           manager->introspection_data->interfaces[0],
                                           &interface_vtable,
                                           manager,
                                           NULL,
                                           NULL);

        manager->name_id = g_bus_own_name_on_connection (connection,
                                                               GSD_SHARING_DBUS_NAME,
                                                               G_BUS_NAME_OWNER_FLAGS_NONE,
                                                               NULL,
                                                               NULL,
                                                               NULL,
                                                               NULL);
}

#if HAVE_NETWORK_MANAGER
static void
primary_connection_changed (GObject    *gobject,
                            GParamSpec *pspec,
                            gpointer    user_data)
{
        GsdSharingManager *manager = user_data;
        NMActiveConnection *a_con;

        a_con = nm_client_get_primary_connection (manager->client);

        g_clear_pointer (&manager->current_network, g_free);
        g_clear_pointer (&manager->current_network_name, g_free);
        g_clear_pointer (&manager->carrier_type, g_free);

        if (a_con) {
                manager->current_network = g_strdup (nm_active_connection_get_uuid (a_con));
                manager->current_network_name = g_strdup (nm_active_connection_get_id (a_con));
                manager->carrier_type = g_strdup (nm_active_connection_get_connection_type (a_con));
                if (manager->carrier_type == NULL)
                        manager->carrier_type = g_strdup ("");
        } else {
                manager->current_network = g_strdup ("");
                manager->current_network_name = g_strdup ("");
                manager->carrier_type = g_strdup ("");
        }

        if (!a_con) {
                manager->sharing_status = GSD_SHARING_STATUS_OFFLINE;
        } else if (*(manager->carrier_type) == '\0') {
                /* Missing carrier type information? */
                manager->sharing_status = GSD_SHARING_STATUS_OFFLINE;
        } else if (g_str_equal (manager->carrier_type, "bluetooth") ||
                   g_str_equal (manager->carrier_type, "gsm") ||
                   g_str_equal (manager->carrier_type, "cdma")) {
                manager->sharing_status = GSD_SHARING_STATUS_DISABLED_MOBILE_BROADBAND;
        } else if (g_str_equal (manager->carrier_type, "802-11-wireless")) {
                if (connection_is_low_security (manager, manager->current_network))
                        manager->sharing_status = GSD_SHARING_STATUS_DISABLED_LOW_SECURITY;
                else
                        manager->sharing_status = GSD_SHARING_STATUS_AVAILABLE;
        } else {
                manager->sharing_status = GSD_SHARING_STATUS_AVAILABLE;
        }

        g_debug ("current network: %s", manager->current_network);
        g_debug ("current network name: %s", manager->current_network_name);
        g_debug ("conn type: %s", manager->carrier_type);
        g_debug ("status: %d", manager->sharing_status);

        properties_changed (manager);
        gsd_sharing_manager_sync_services (manager);
}

static void
nm_client_ready (GObject      *source_object,
                 GAsyncResult *res,
                 gpointer      user_data)
{
        GsdSharingManager *manager = user_data;
        GError *error = NULL;
        NMClient *client;

        client = nm_client_new_finish (res, &error);
        if (!client) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Couldn't get NMClient: %s", error->message);
                g_error_free (error);
                return;
        }
        manager->client = client;

        g_signal_connect (G_OBJECT (client), "notify::primary-connection",
                          G_CALLBACK (primary_connection_changed), manager);

        primary_connection_changed (NULL, NULL, manager);
}

#endif /* HAVE_NETWORK_MANAGER */

#define RYGEL_BUS_NAME "org.gnome.Rygel1"
#define RYGEL_OBJECT_PATH "/org/gnome/Rygel1"
#define RYGEL_INTERFACE_NAME "org.gnome.Rygel1"

static void
gsd_sharing_manager_disable_rygel (void)
{
	GDBusConnection *connection;
	gchar *path;

	path = g_build_filename (g_get_user_config_dir (), "autostart",
				 "rygel.desktop", NULL);
	if (!g_file_test (path, G_FILE_TEST_IS_SYMLINK | G_FILE_TEST_IS_REGULAR))
                goto out;

        g_unlink (path);

	connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
	if (connection) {
		g_dbus_connection_call (connection, RYGEL_BUS_NAME, RYGEL_OBJECT_PATH, RYGEL_INTERFACE_NAME,
					"Shutdown", NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1,
					NULL, NULL, NULL);
	}
	g_object_unref (connection);

 out:
        g_free (path);
}

static void
gsd_sharing_manager_startup (GApplication *app)
{
        GsdSharingManager *manager = GSD_SHARING_MANAGER (app);

        g_debug ("Starting sharing manager");
        gnome_settings_profile_start (NULL);

        manager->introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
        g_assert (manager->introspection_data != NULL);

        gsd_sharing_manager_disable_rygel ();

        manager->cancellable = g_cancellable_new ();
#if HAVE_NETWORK_MANAGER
        nm_client_new_async (manager->cancellable, nm_client_ready, manager);
#endif /* HAVE_NETWORK_MANAGER */

        /* Start process of owning a D-Bus name */
        g_bus_get (G_BUS_TYPE_SESSION,
                   manager->cancellable,
                   (GAsyncReadyCallback) on_bus_gotten,
                   manager);

        G_APPLICATION_CLASS (gsd_sharing_manager_parent_class)->startup (app);

        gnome_settings_profile_end (NULL);
}

static void
cancel_pending_wait_tasks (GsdSharingManager *manager)
{
        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init (&iter, manager->assigned_services);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                AssignedServiceInfo *info = value;
                g_cancellable_cancel (info->cancellable);
        }
}

static void
gsd_sharing_manager_pre_shutdown (GsdApplication *app)
{
        GsdSharingManager *manager = GSD_SHARING_MANAGER (app);

        g_debug ("Pre-shutdown on sharing manager");

        cancel_pending_wait_tasks (manager);

        if (manager->sharing_status != GSD_SHARING_STATUS_OFFLINE &&
            manager->connection != NULL) {
                manager->sharing_status = GSD_SHARING_STATUS_OFFLINE;
                gsd_sharing_manager_sync_services (manager);
        }

        GSD_APPLICATION_CLASS (gsd_sharing_manager_parent_class)->pre_shutdown (app);
}

static void
gsd_sharing_manager_shutdown (GApplication *app)
{
        GsdSharingManager *manager = GSD_SHARING_MANAGER (app);

        g_debug ("Stopping sharing manager");

        if (manager->cancellable) {
                g_cancellable_cancel (manager->cancellable);
                g_clear_object (&manager->cancellable);
        }

#if HAVE_NETWORK_MANAGER
        g_clear_object (&manager->client);
#endif /* HAVE_NETWORK_MANAGER */

        if (manager->name_id != 0) {
                g_bus_unown_name (manager->name_id);
                manager->name_id = 0;
        }

        g_clear_pointer (&manager->introspection_data, g_dbus_node_info_unref);
        g_clear_object (&manager->connection);

        g_clear_pointer (&manager->current_network, g_free);
        g_clear_pointer (&manager->current_network_name, g_free);
        g_clear_pointer (&manager->carrier_type, g_free);

        G_APPLICATION_CLASS (gsd_sharing_manager_parent_class)->shutdown (app);
}

static void
gsd_sharing_manager_class_init (GsdSharingManagerClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        GsdApplicationClass *gsd_application_class = GSD_APPLICATION_CLASS (klass);
        GApplicationClass *application_class = G_APPLICATION_CLASS (klass);

        object_class->finalize = gsd_sharing_manager_finalize;

        application_class->startup = gsd_sharing_manager_startup;
        application_class->shutdown = gsd_sharing_manager_shutdown;

        gsd_application_class->pre_shutdown = gsd_sharing_manager_pre_shutdown;
}

static void
configurable_service_free (gpointer pointer)
{
        ConfigurableServiceInfo *service = pointer;

        g_clear_object (&service->settings);
        g_free (service);
}

static void
assigned_service_free (gpointer pointer)
{
        AssignedServiceInfo *info = pointer;

        g_cancellable_cancel (info->cancellable);
        g_clear_object (&info->cancellable);

        g_bus_unwatch_name (info->system_bus_name_watch);
        g_free (info);
}

#if HAVE_SYSTEMD_LIB
static void
on_system_bus_name_appeared (GDBusConnection   *connection,
                             const char        *system_bus_name,
                             const char        *system_bus_name_owner,
                             gpointer           user_data)
{
        GsdSharingManager *manager = user_data;
        AssignedServiceInfo *info;

        info = g_hash_table_lookup (manager->assigned_services, system_bus_name);

        if (info == NULL)
                return;

        if (info->system_service_running)
                return;

        info->system_service_running = TRUE;

        start_assigned_service (manager, info);
}

static void
on_system_bus_name_vanished (GDBusConnection   *connection,
                             const char        *system_bus_name,
                             gpointer           user_data)
{
        GsdSharingManager *manager = user_data;
        AssignedServiceInfo *info;

        info = g_hash_table_lookup (manager->assigned_services, system_bus_name);

        if (info == NULL)
                return;

        if (!info->system_service_running)
                return;

        info->system_service_running = FALSE;

        stop_assigned_service_after_timeout (manager, info);
}
#endif

static void
manage_configurable_services (GsdSharingManager *manager)
{
        size_t i;

        for (i = 0; i < G_N_ELEMENTS (configurable_services); i++) {
                ConfigurableServiceInfo *service;
                char *path;

                service = g_new0 (ConfigurableServiceInfo, 1);
                service->name = configurable_services[i];
                path = g_strdup_printf ("/org/gnome/settings-daemon/plugins/sharing/%s/", configurable_services[i]);
                service->settings = g_settings_new_with_path ("org.gnome.settings-daemon.plugins.sharing.service", path);
                g_free (path);

                g_hash_table_insert (manager->configurable_services, (gpointer) configurable_services[i], service);
        }
}

static void
manage_assigned_services (GsdSharingManager *manager)
{
#if HAVE_SYSTEMD_LIB
        size_t i;
        int ret;
        g_autofree char *session_id = NULL;
        g_autofree char *session_class = NULL;
        gboolean is_remote;

        if (manager->is_systemd_managed)
                ret = sd_uid_get_display (getuid (), &session_id);
        else
                ret = sd_pid_get_session (getpid (), &session_id);

        if (ret != 0) {
                g_warning ("Failed to find systemd session id: %s", g_strerror (-ret));
                return;
        }

        ret = sd_session_get_class (session_id, &session_class);

        if (ret != 0) {
                g_warning ("Failed to find systemd session class for session %s: %s", session_id, g_strerror (-ret));
                return;
        }

        ret = sd_session_is_remote (session_id);

        if (ret < 0) {
                g_warning ("Failed to find out if systemd session %s is remote: %s", session_id, g_strerror (-ret));
                return;
        }

        is_remote = ret;

        for (i = 0; i < G_N_ELEMENTS (assigned_services); i++) {
                AssignedServiceInfo *info;
                AssignedService *service;
                const char * const *session_classes;

                service = &assigned_services[i];

                if (is_remote)
                        session_classes = (const char * const *) service->remote_session_classes;
                else
                        session_classes = (const char * const *) service->local_session_classes;

                if (!g_strv_contains (session_classes, session_class))
                        continue;

                info = g_new0 (AssignedServiceInfo, 1);
                info->service = service;

                info->system_bus_name_watch = g_bus_watch_name(G_BUS_TYPE_SYSTEM,
                                                               service->system_bus_name,
                                                               G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                               on_system_bus_name_appeared,
                                                               on_system_bus_name_vanished,
                                                               manager,
                                                               NULL);

                g_hash_table_insert (manager->assigned_services, (gpointer) service->system_bus_name, info);
        }
#endif
}

static void
gsd_sharing_manager_init (GsdSharingManager *manager)
{
        int ret = -1;
        g_autofree char *systemd_unit = NULL;

        manager->configurable_services = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, configurable_service_free);
        manager->assigned_services = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, assigned_service_free);

        /* Default state */
        manager->current_network = g_strdup ("");
        manager->current_network_name = g_strdup ("");
        manager->carrier_type = g_strdup ("");
        manager->sharing_status = GSD_SHARING_STATUS_OFFLINE;

#if HAVE_SYSTEMD_LIB
        ret = sd_pid_get_user_unit (getpid (), &systemd_unit);
#endif

        if (ret < 0)
                manager->is_systemd_managed = FALSE;
        else
                manager->is_systemd_managed = TRUE;

        manage_configurable_services (manager);
        manage_assigned_services (manager);
}

static void
gsd_sharing_manager_finalize (GObject *object)
{
        GsdSharingManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_SHARING_MANAGER (object));

        manager = GSD_SHARING_MANAGER (object);

        g_return_if_fail (manager != NULL);

        g_hash_table_unref (manager->configurable_services);
        g_hash_table_unref (manager->assigned_services);

        G_OBJECT_CLASS (gsd_sharing_manager_parent_class)->finalize (object);
}
