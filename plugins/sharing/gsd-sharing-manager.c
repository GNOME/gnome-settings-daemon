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

#ifdef HAVE_NETWORK_MANAGER
#include <NetworkManager.h>
#endif /* HAVE_NETWORK_MANAGER */

#include "gnome-settings-profile.h"
#include "gsd-sharing-manager.h"
#include "gsd-sharing-enums.h"

#define GSD_SHARING_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_SHARING_MANAGER, GsdSharingManagerPrivate))

typedef struct {
        const char  *name;
        GSettings   *settings;
} ServiceInfo;

struct GsdSharingManagerPrivate
{
        GDBusNodeInfo           *introspection_data;
        guint                    name_id;
        GDBusConnection         *connection;

        GCancellable            *cancellable;
#ifdef HAVE_NETWORK_MANAGER
        NMClient                *client;
#endif /* HAVE_NETWORK_MANAGER */

        GHashTable              *services;

        char                    *current_network;
        char                    *current_network_name;
        char                    *carrier_type;
        GsdSharingStatus         sharing_status;
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

G_DEFINE_TYPE (GsdSharingManager, gsd_sharing_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static const char * const services[] = {
        "rygel",
        "vino-server",
        "gnome-remote-desktop",
        "gnome-user-share-webdav"
};

static void
handle_unit_cb (GObject      *source_object,
                GAsyncResult *res,
                gpointer      user_data)
{
        GError *error = NULL;
        GVariant *ret;
        const char *operation = user_data;

        ret = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object),
                                             res, &error);
        if (!ret) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Failed to %s service: %s", operation, error->message);
                g_error_free (error);
                return;
        }

        g_variant_unref (ret);

}

static void
gsd_sharing_manager_handle_service (GsdSharingManager   *manager,
                                    const char          *method,
                                    ServiceInfo         *service)
{
        char *service_file;

        service_file = g_strdup_printf ("%s.service", service->name);
        g_dbus_connection_call (manager->priv->connection,
                                "org.freedesktop.systemd1",
                                "/org/freedesktop/systemd1",
                                "org.freedesktop.systemd1.Manager",
                                method,
                                g_variant_new ("(ss)", service_file, "replace"),
                                NULL,
                                G_DBUS_CALL_FLAGS_NONE,
                                -1,
                                manager->priv->cancellable,
                                handle_unit_cb,
                                (gpointer) method);
        g_free (service_file);
}

static void
gsd_sharing_manager_start_service (GsdSharingManager *manager,
                                   ServiceInfo       *service)
{
        g_debug ("About to start %s", service->name);

        /* We use StartUnit, not StartUnitReplace, since the latter would
         * cancel any pending start we already have going from an
         * earlier _start_service() call */
        gsd_sharing_manager_handle_service (manager, "StartUnit", service);
}

static void
gsd_sharing_manager_stop_service (GsdSharingManager *manager,
                                  ServiceInfo       *service)
{
        g_debug ("About to stop %s", service->name);

        gsd_sharing_manager_handle_service (manager, "StopUnit", service);
}

#ifdef HAVE_NETWORK_MANAGER
static gboolean
service_is_enabled_on_current_connection (GsdSharingManager *manager,
                                          ServiceInfo       *service)
{
        char **connections;
        int j;
        gboolean ret;
        connections = g_settings_get_strv (service->settings, "enabled-connections");
        ret = FALSE;
        for (j = 0; connections[j] != NULL; j++) {
                if (g_strcmp0 (connections[j], manager->priv->current_network) == 0) {
                        ret = TRUE;
                        break;
                }
        }

        g_strfreev (connections);
        return ret;
}
#else
static gboolean
service_is_enabled_on_current_connection (GsdSharingManager *manager,
                                          ServiceInfo       *service)
{
        return FALSE;
}
#endif /* HAVE_NETWORK_MANAGER */

static void
gsd_sharing_manager_sync_services (GsdSharingManager *manager)
{
        GList *services, *l;

        services = g_hash_table_get_values (manager->priv->services);

        for (l = services; l != NULL; l = l->next) {
                ServiceInfo *service = l->data;
                gboolean should_be_started = FALSE;

                if (manager->priv->sharing_status == GSD_SHARING_STATUS_AVAILABLE &&
                    service_is_enabled_on_current_connection (manager, service))
                        should_be_started = TRUE;

                if (should_be_started)
                        gsd_sharing_manager_start_service (manager, service);
                else
                        gsd_sharing_manager_stop_service (manager, service);
        }
        g_list_free (services);
}

#ifdef HAVE_NETWORK_MANAGER
static void
properties_changed (GsdSharingManager *manager)
{
        GVariantBuilder props_builder;
        GVariant *props_changed = NULL;

        /* not yet connected to the session bus */
        if (manager->priv->connection == NULL)
                return;

        g_variant_builder_init (&props_builder, G_VARIANT_TYPE ("a{sv}"));

        g_variant_builder_add (&props_builder, "{sv}", "CurrentNetwork",
                               g_variant_new_string (manager->priv->current_network));
        g_variant_builder_add (&props_builder, "{sv}", "CurrentNetworkName",
                               g_variant_new_string (manager->priv->current_network_name));
        g_variant_builder_add (&props_builder, "{sv}", "CarrierType",
                               g_variant_new_string (manager->priv->carrier_type));
        g_variant_builder_add (&props_builder, "{sv}", "SharingStatus",
                               g_variant_new_uint32 (manager->priv->sharing_status));

        props_changed = g_variant_new ("(s@a{sv}@as)", GSD_SHARING_DBUS_NAME,
                                       g_variant_builder_end (&props_builder),
                                       g_variant_new_strv (NULL, 0));

        g_dbus_connection_emit_signal (manager->priv->connection,
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
        ServiceInfo *service;

        service = g_hash_table_lookup (manager->priv->services, service_name);
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
        if (g_hash_table_lookup (manager->priv->services, service_name))
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
        ServiceInfo *service;
        char **connections;
        GPtrArray *array;
        guint i;

        if (!check_service (manager, service_name, error))
                return FALSE;

        if (manager->priv->sharing_status != GSD_SHARING_STATUS_AVAILABLE) {
                g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                             "Sharing cannot be enabled on this network, status is '%d'", manager->priv->sharing_status);
                return FALSE;
        }

        service = g_hash_table_lookup (manager->priv->services, service_name);
        connections = g_settings_get_strv (service->settings, "enabled-connections");
        array = g_ptr_array_new ();
        for (i = 0; connections[i] != NULL; i++) {
                if (g_strcmp0 (connections[i], manager->priv->current_network) == 0)
                        goto bail;
                g_ptr_array_add (array, connections[i]);
        }
        g_ptr_array_add (array, manager->priv->current_network);
        g_ptr_array_add (array, NULL);

        g_settings_set_strv (service->settings, "enabled-connections", (const gchar *const *) array->pdata);

bail:

        gsd_sharing_manager_start_service (manager, service);

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
        ServiceInfo *service;
        char **connections;
        GPtrArray *array;
        guint i;

        if (!check_service (manager, service_name, error))
                return FALSE;

        service = g_hash_table_lookup (manager->priv->services, service_name);
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

        if (g_str_equal (network_name, manager->priv->current_network))
                gsd_sharing_manager_stop_service (manager, service);

        return TRUE;
}

#ifdef HAVE_NETWORK_MANAGER
static const char *
get_type_and_name_for_connection_uuid (GsdSharingManager *manager,
                                       const char        *uuid,
                                       const char       **name)
{
        NMRemoteConnection *conn;
        const char *type;

        if (!manager->priv->client)
                return NULL;

        conn = nm_client_get_connection_by_uuid (manager->priv->client, uuid);
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

#ifdef HAVE_NETWORK_MANAGER
static gboolean
connection_is_low_security (GsdSharingManager *manager,
                            const char        *uuid)
{
        NMRemoteConnection *conn;

        if (!manager->priv->client)
                return TRUE;

        conn = nm_client_get_connection_by_uuid (manager->priv->client, uuid);
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

#ifdef HAVE_NETWORK_MANAGER
        if (!manager->priv->client) {
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
        if (manager->priv->connection == NULL)
                return NULL;

        if (g_strcmp0 (property_name, "CurrentNetwork") == 0) {
                return g_variant_new_string (manager->priv->current_network);
        }

        if (g_strcmp0 (property_name, "CurrentNetworkName") == 0) {
                return g_variant_new_string (manager->priv->current_network_name);
        }

        if (g_strcmp0 (property_name, "CarrierType") == 0) {
                return g_variant_new_string (manager->priv->carrier_type);
        }

        if (g_strcmp0 (property_name, "SharingStatus") == 0) {
                return g_variant_new_uint32 (manager->priv->sharing_status);
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
        if (manager->priv->connection == NULL)
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
        manager->priv->connection = connection;

        g_dbus_connection_register_object (connection,
                                           GSD_SHARING_DBUS_PATH,
                                           manager->priv->introspection_data->interfaces[0],
                                           &interface_vtable,
                                           manager,
                                           NULL,
                                           NULL);

        manager->priv->name_id = g_bus_own_name_on_connection (connection,
                                                               GSD_SHARING_DBUS_NAME,
                                                               G_BUS_NAME_OWNER_FLAGS_NONE,
                                                               NULL,
                                                               NULL,
                                                               NULL,
                                                               NULL);
}

#ifdef HAVE_NETWORK_MANAGER
static void
primary_connection_changed (GObject    *gobject,
                            GParamSpec *pspec,
                            gpointer    user_data)
{
        GsdSharingManager *manager = user_data;
        NMActiveConnection *a_con;

        a_con = nm_client_get_primary_connection (manager->priv->client);

        g_clear_pointer (&manager->priv->current_network, g_free);
        g_clear_pointer (&manager->priv->current_network_name, g_free);
        g_clear_pointer (&manager->priv->carrier_type, g_free);

        if (a_con) {
                manager->priv->current_network = g_strdup (nm_active_connection_get_uuid (a_con));
                manager->priv->current_network_name = g_strdup (nm_active_connection_get_id (a_con));
                manager->priv->carrier_type = g_strdup (nm_active_connection_get_connection_type (a_con));
                if (manager->priv->carrier_type == NULL)
                        manager->priv->carrier_type = g_strdup ("");
        } else {
                manager->priv->current_network = g_strdup ("");
                manager->priv->current_network_name = g_strdup ("");
                manager->priv->carrier_type = g_strdup ("");
        }

        if (!a_con) {
                manager->priv->sharing_status = GSD_SHARING_STATUS_OFFLINE;
        } else if (*(manager->priv->carrier_type) == '\0') {
                /* Missing carrier type information? */
                manager->priv->sharing_status = GSD_SHARING_STATUS_OFFLINE;
        } else if (g_str_equal (manager->priv->carrier_type, "bluetooth") ||
                   g_str_equal (manager->priv->carrier_type, "gsm") ||
                   g_str_equal (manager->priv->carrier_type, "cdma")) {
                manager->priv->sharing_status = GSD_SHARING_STATUS_DISABLED_MOBILE_BROADBAND;
        } else if (g_str_equal (manager->priv->carrier_type, "802-11-wireless")) {
                if (connection_is_low_security (manager, manager->priv->current_network))
                        manager->priv->sharing_status = GSD_SHARING_STATUS_DISABLED_LOW_SECURITY;
                else
                        manager->priv->sharing_status = GSD_SHARING_STATUS_AVAILABLE;
        } else {
                manager->priv->sharing_status = GSD_SHARING_STATUS_AVAILABLE;
        }

        g_debug ("current network: %s", manager->priv->current_network);
        g_debug ("current network name: %s", manager->priv->current_network_name);
        g_debug ("conn type: %s", manager->priv->carrier_type);
        g_debug ("status: %d", manager->priv->sharing_status);

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
        manager->priv->client = client;

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

gboolean
gsd_sharing_manager_start (GsdSharingManager *manager,
                           GError           **error)
{
        g_debug ("Starting sharing manager");
        gnome_settings_profile_start (NULL);

        manager->priv->introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
        g_assert (manager->priv->introspection_data != NULL);

        gsd_sharing_manager_disable_rygel ();

        manager->priv->cancellable = g_cancellable_new ();
#ifdef HAVE_NETWORK_MANAGER
        nm_client_new_async (manager->priv->cancellable, nm_client_ready, manager);
#endif /* HAVE_NETWORK_MANAGER */

        /* Start process of owning a D-Bus name */
        g_bus_get (G_BUS_TYPE_SESSION,
                   manager->priv->cancellable,
                   (GAsyncReadyCallback) on_bus_gotten,
                   manager);

        gnome_settings_profile_end (NULL);
        return TRUE;
}

void
gsd_sharing_manager_stop (GsdSharingManager *manager)
{
        g_debug ("Stopping sharing manager");

        if (manager->priv->sharing_status == GSD_SHARING_STATUS_AVAILABLE &&
            manager->priv->connection != NULL) {
                manager->priv->sharing_status = GSD_SHARING_STATUS_OFFLINE;
                gsd_sharing_manager_sync_services (manager);
        }

        if (manager->priv->cancellable) {
                g_cancellable_cancel (manager->priv->cancellable);
                g_clear_object (&manager->priv->cancellable);
        }

#ifdef HAVE_NETWORK_MANAGER
        g_clear_object (&manager->priv->client);
#endif /* HAVE_NETWORK_MANAGER */

        if (manager->priv->name_id != 0) {
                g_bus_unown_name (manager->priv->name_id);
                manager->priv->name_id = 0;
        }

        g_clear_pointer (&manager->priv->introspection_data, g_dbus_node_info_unref);
        g_clear_object (&manager->priv->connection);

        g_clear_pointer (&manager->priv->current_network, g_free);
        g_clear_pointer (&manager->priv->current_network_name, g_free);
        g_clear_pointer (&manager->priv->carrier_type, g_free);
}

static void
gsd_sharing_manager_class_init (GsdSharingManagerClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_sharing_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdSharingManagerPrivate));
}

static void
service_free (gpointer pointer)
{
        ServiceInfo *service = pointer;

        g_clear_object (&service->settings);
        g_free (service);
}

static void
gsd_sharing_manager_init (GsdSharingManager *manager)
{
        guint i;

        manager->priv = GSD_SHARING_MANAGER_GET_PRIVATE (manager);
        manager->priv->services = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, service_free);

        /* Default state */
        manager->priv->current_network = g_strdup ("");
        manager->priv->current_network_name = g_strdup ("");
        manager->priv->carrier_type = g_strdup ("");
        manager->priv->sharing_status = GSD_SHARING_STATUS_OFFLINE;

        for (i = 0; i < G_N_ELEMENTS (services); i++) {
                ServiceInfo *service;
                char *path;

                service = g_new0 (ServiceInfo, 1);
                service->name = services[i];
                path = g_strdup_printf ("/org/gnome/settings-daemon/plugins/sharing/%s/", services[i]);
                service->settings = g_settings_new_with_path ("org.gnome.settings-daemon.plugins.sharing.service", path);
                g_free (path);

                g_hash_table_insert (manager->priv->services, (gpointer) services[i], service);
        }
}

static void
gsd_sharing_manager_finalize (GObject *object)
{
        GsdSharingManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_SHARING_MANAGER (object));

        manager = GSD_SHARING_MANAGER (object);

        g_return_if_fail (manager->priv != NULL);

        gsd_sharing_manager_stop (manager);

        g_hash_table_unref (manager->priv->services);

        G_OBJECT_CLASS (gsd_sharing_manager_parent_class)->finalize (object);
}

GsdSharingManager *
gsd_sharing_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_SHARING_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_SHARING_MANAGER (manager_object);
}
