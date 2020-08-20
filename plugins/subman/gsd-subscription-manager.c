/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Kalev Lember <klember@redhat.com>
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

#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <libnotify/notify.h>

#include "gnome-settings-profile.h"
#include "gsd-subman-common.h"
#include "gsd-subscription-manager.h"

#define GSD_DBUS_NAME "org.gnome.SettingsDaemon"
#define GSD_DBUS_PATH "/org/gnome/SettingsDaemon"
#define GSD_DBUS_BASE_INTERFACE "org.gnome.SettingsDaemon"

#define GSD_SUBSCRIPTION_DBUS_NAME		GSD_DBUS_NAME ".Subscription"
#define GSD_SUBSCRIPTION_DBUS_PATH		GSD_DBUS_PATH "/Subscription"
#define GSD_SUBSCRIPTION_DBUS_INTERFACE		GSD_DBUS_BASE_INTERFACE ".Subscription"

static const gchar introspection_xml[] =
"<node>"
"  <interface name='org.gnome.SettingsDaemon.Subscription'>"
"    <method name='Register'>"
"      <arg type='a{sv}' name='options' direction='in'/>"
"    </method>"
"    <method name='Unregister'/>"
"    <property name='InstalledProducts' type='aa{sv}' access='read'/>"
"    <property name='SubscriptionStatus' type='u' access='read'/>"
"  </interface>"
"</node>";

#define GSD_SUBSCRIPTION_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_SUBSCRIPTION_MANAGER, GsdSubscriptionManagerPrivate))

typedef enum {
	_RHSM_INTERFACE_CONFIG,
	_RHSM_INTERFACE_REGISTER_SERVER,
	_RHSM_INTERFACE_ATTACH,
	_RHSM_INTERFACE_ENTITLEMENT,
	_RHSM_INTERFACE_PRODUCTS,
	_RHSM_INTERFACE_CONSUMER,
	_RHSM_INTERFACE_SYSPURPOSE,
	_RHSM_INTERFACE_LAST
} _RhsmInterface;

struct GsdSubscriptionManagerPrivate
{
	/* D-Bus */
	guint		 name_id;
	GDBusNodeInfo	*introspection_data;
	GDBusConnection	*connection;
	GCancellable	*bus_cancellable;

	GDBusProxy	*proxies[_RHSM_INTERFACE_LAST];
	GHashTable	*config; 	/* str:str */
	GPtrArray	*installed_products;
	gchar		*address;

	GTimer		*timer_last_notified;
	NotifyNotification	*notification_expired;
	NotifyNotification	*notification_registered;
	NotifyNotification	*notification_registration_required;
	GsdSubmanSubscriptionStatus	 subscription_status;
	GsdSubmanSubscriptionStatus	 subscription_status_last;
};

enum {
	PROP_0,
};

static void     gsd_subscription_manager_class_init  (GsdSubscriptionManagerClass *klass);
static void     gsd_subscription_manager_init        (GsdSubscriptionManager      *subscription_manager);
static void     gsd_subscription_manager_finalize    (GObject             *object);

G_DEFINE_TYPE (GsdSubscriptionManager, gsd_subscription_manager, G_TYPE_OBJECT)

typedef struct
{
	gchar *product_name;
	gchar *product_id;
	gchar *version;
	gchar *arch;
	gchar *status;
	gchar *starts;
	gchar *ends;
} ProductData;

static void
product_data_free (ProductData *product)
{
	g_free (product->product_name);
	g_free (product->product_id);
	g_free (product->version);
	g_free (product->arch);
	g_free (product->status);
	g_free (product->starts);
	g_free (product->ends);
	g_free (product);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ProductData, product_data_free);

static gpointer manager_object = NULL;

GQuark
gsd_subscription_manager_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("gsd_subscription_manager_error");
	return quark;
}

static GsdSubmanSubscriptionStatus
_client_subscription_status_from_text (const gchar *status_txt)
{
	if (g_strcmp0 (status_txt, "Unknown") == 0)
		return GSD_SUBMAN_SUBSCRIPTION_STATUS_UNKNOWN;
	if (g_strcmp0 (status_txt, "Current") == 0)
		return GSD_SUBMAN_SUBSCRIPTION_STATUS_VALID;
	if (g_strcmp0 (status_txt, "Invalid") == 0)
		return GSD_SUBMAN_SUBSCRIPTION_STATUS_INVALID;
	if (g_strcmp0 (status_txt, "Disabled") == 0)
		return GSD_SUBMAN_SUBSCRIPTION_STATUS_DISABLED;
	if (g_strcmp0 (status_txt, "Insufficient") == 0)
		return GSD_SUBMAN_SUBSCRIPTION_STATUS_PARTIALLY_VALID;
	g_warning ("Unknown subscription status: %s", status_txt); // 'Current'?
	return GSD_SUBMAN_SUBSCRIPTION_STATUS_UNKNOWN;
}

static GVariant *
_make_installed_products_variant (GPtrArray *installed_products)
{
	GVariantBuilder builder;
	g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));

	for (guint i = 0; i < installed_products->len; i++) {
		ProductData *product = g_ptr_array_index (installed_products, i);
		g_auto(GVariantDict) dict;

		g_variant_dict_init (&dict, NULL);

		g_variant_dict_insert (&dict, "product-name", "s", product->product_name);
		g_variant_dict_insert (&dict, "product-id", "s", product->product_id);
		g_variant_dict_insert (&dict, "version", "s", product->version);
		g_variant_dict_insert (&dict, "arch", "s", product->arch);
		g_variant_dict_insert (&dict, "status", "s", product->status);
		g_variant_dict_insert (&dict, "starts", "s", product->starts);
		g_variant_dict_insert (&dict, "ends", "s", product->ends);

		g_variant_builder_add_value (&builder, g_variant_dict_end (&dict));
	}

	return g_variant_builder_end (&builder);
}

static void
_emit_property_changed (GsdSubscriptionManager *manager,
		        const gchar *property_name,
		        GVariant *property_value)
{
	GsdSubscriptionManagerPrivate *priv = manager->priv;
	GVariantBuilder builder;
	GVariantBuilder invalidated_builder;

	/* not yet connected */
	if (priv->connection == NULL)
		return;

	/* build the dict */
	g_variant_builder_init (&invalidated_builder, G_VARIANT_TYPE ("as"));
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	g_variant_builder_add (&builder,
			       "{sv}",
			       property_name,
			       property_value);
	g_dbus_connection_emit_signal (priv->connection,
				       NULL,
				       GSD_SUBSCRIPTION_DBUS_PATH,
				       "org.freedesktop.DBus.Properties",
				       "PropertiesChanged",
				       g_variant_new ("(sa{sv}as)",
				       GSD_SUBSCRIPTION_DBUS_INTERFACE,
				       &builder,
				       &invalidated_builder),
				       NULL);
	g_variant_builder_clear (&builder);
	g_variant_builder_clear (&invalidated_builder);
}

static gboolean
_client_installed_products_update (GsdSubscriptionManager *manager, GError **error)
{
	GsdSubscriptionManagerPrivate *priv = manager->priv;
	JsonNode *json_root;
	JsonArray *json_products_array;
	const gchar *json_txt = NULL;
	g_autoptr(GVariant) val = NULL;
	g_autoptr(JsonParser) json_parser = json_parser_new ();

	val = g_dbus_proxy_call_sync (priv->proxies[_RHSM_INTERFACE_PRODUCTS],
				      "ListInstalledProducts",
				      g_variant_new ("(sa{sv}s)",
						     ""   /* filter_string */,
						     NULL /* proxy_options */,
						     "C.UTF-8"),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1, NULL, error);
	if (val == NULL)
		return FALSE;
	g_variant_get (val, "(&s)", &json_txt);
	g_debug ("Products.ListInstalledProducts JSON: %s", json_txt);
	if (!json_parser_load_from_data (json_parser, json_txt, -1, error))
		return FALSE;
	json_root = json_parser_get_root (json_parser);
	json_products_array = json_node_get_array (json_root);
	if (json_products_array == NULL) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			     "no InstalledProducts array in %s", json_txt);
		return FALSE;
	}

	g_ptr_array_set_size (priv->installed_products, 0);

	for (guint i = 0; i < json_array_get_length (json_products_array); i++) {
		JsonArray *json_product = json_array_get_array_element (json_products_array, i);
		g_autoptr(ProductData) product = g_new0 (ProductData, 1);

		if (json_product == NULL)
			continue;
		if (json_array_get_length (json_product) < 8) {
			g_debug ("Unexpected number of array elements in InstalledProducts JSON");
			continue;
		}

		product->product_name = g_strdup (json_array_get_string_element (json_product, 0));
		product->product_id = g_strdup (json_array_get_string_element (json_product, 1));
		product->version = g_strdup (json_array_get_string_element (json_product, 2));
		product->arch = g_strdup (json_array_get_string_element (json_product, 3));
		product->status = g_strdup (json_array_get_string_element (json_product, 4));
		product->starts = g_strdup (json_array_get_string_element (json_product, 6));
		product->ends = g_strdup (json_array_get_string_element (json_product, 7));

		g_ptr_array_add (priv->installed_products, g_steal_pointer (&product));
	}

	/* emit notification for g-c-c */
	_emit_property_changed (manager, "InstalledProducts",
			       _make_installed_products_variant (priv->installed_products));

	return TRUE;
}

static gboolean
_client_subscription_status_update (GsdSubscriptionManager *manager, GError **error)
{
	GsdSubscriptionManagerPrivate *priv = manager->priv;
	JsonNode *json_root;
	JsonObject *json_obj;
	const gchar *json_txt = NULL;
	const gchar *status_txt = NULL;
	g_autoptr(GVariant) val = NULL;
	g_autoptr(JsonParser) json_parser = json_parser_new ();

	/* save old value */
	priv->subscription_status_last = priv->subscription_status;

	val = g_dbus_proxy_call_sync (priv->proxies[_RHSM_INTERFACE_ENTITLEMENT],
				      "GetStatus",
				      g_variant_new ("(ss)",
						     "", /* assumed as 'now' */
						     "C.UTF-8"),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1, NULL, error);
	if (val == NULL)
		return FALSE;
	g_variant_get (val, "(&s)", &json_txt);
	g_debug ("Entitlement.GetStatus JSON: %s", json_txt);
	if (!json_parser_load_from_data (json_parser, json_txt, -1, error))
		return FALSE;
	json_root = json_parser_get_root (json_parser);
	json_obj = json_node_get_object (json_root);
	if (!json_object_has_member (json_obj, "status")) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			     "no Entitlement.GetStatus status in %s", json_txt);
		return FALSE;
	}

	status_txt = json_object_get_string_member (json_obj, "status");
	g_debug ("Entitlement.GetStatus: %s", status_txt);
	priv->subscription_status = _client_subscription_status_from_text (status_txt);

	/* emit notification for g-c-c */
	if (priv->subscription_status != priv->subscription_status_last) {
		_emit_property_changed (manager, "SubscriptionStatus",
				       g_variant_new_uint32 (priv->subscription_status));
	}

	return TRUE;
}

static gboolean
_client_syspurpose_update (GsdSubscriptionManager *manager, GError **error)
{
	GsdSubscriptionManagerPrivate *priv = manager->priv;
	JsonNode *json_root;
	JsonObject *json_obj;
	const gchar *json_txt = NULL;
	g_autoptr(GVariant) val = NULL;
	g_autoptr(JsonParser) json_parser = json_parser_new ();

	val = g_dbus_proxy_call_sync (priv->proxies[_RHSM_INTERFACE_SYSPURPOSE],
				      "GetSyspurpose",
				      g_variant_new ("(s)", "C.UTF-8"),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1, NULL, error);
	if (val == NULL)
		return FALSE;
	g_variant_get (val, "(&s)", &json_txt);
	g_debug ("Syspurpose.GetSyspurpose JSON: %s", json_txt);
	if (!json_parser_load_from_data (json_parser, json_txt, -1, error))
		return FALSE;
	json_root = json_parser_get_root (json_parser);
	json_obj = json_node_get_object (json_root);
	if (!json_object_has_member (json_obj, "status")) {
		g_debug ("Syspurpose.GetSyspurpose: Unknown");
		return TRUE;
	}
	g_debug ("Syspurpose.GetSyspurpose: '%s", json_object_get_string_member (json_obj, "status"));
	return TRUE;
}

static gboolean
_client_register_start (GsdSubscriptionManager *manager, GError **error)
{
	GsdSubscriptionManagerPrivate *priv = manager->priv;
	const gchar *address = NULL;
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GVariant) val = NULL;

	/* already started */
	if (priv->address != NULL)
		return TRUE;

	/* apparently: "we can't send registration credentials over the regular
	 * system or session bus since those aren't really locked down..." */
	proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
					       G_DBUS_PROXY_FLAGS_NONE,
					       NULL,
					       "com.redhat.RHSM1",
					       "/com/redhat/RHSM1/RegisterServer",
					       "com.redhat.RHSM1.RegisterServer",
					       NULL, error);
	if (proxy == NULL)
		return FALSE;
	val = g_dbus_proxy_call_sync (proxy, "Start",
				      g_variant_new ("(s)", "C.UTF-8"),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1, NULL, error);
	if (val == NULL)
		return FALSE;
	g_variant_get (val, "(&s)", &address);
	g_debug ("RegisterServer.Start: %s", address);
	priv->address = g_strdup (address);
	return TRUE;
}

static gboolean
_client_register_stop (GsdSubscriptionManager *manager, GError **error)
{
	GsdSubscriptionManagerPrivate *priv = manager->priv;
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GVariant) val = NULL;

	/* already started */
	if (priv->address == NULL)
		return TRUE;

	/* stop registration server */
	proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
					       G_DBUS_PROXY_FLAGS_NONE,
					       NULL,
					       "com.redhat.RHSM1",
					       "/com/redhat/RHSM1/RegisterServer",
					       "com.redhat.RHSM1.RegisterServer",
					       NULL, error);
	if (proxy == NULL)
		return FALSE;
	val = g_dbus_proxy_call_sync (proxy, "Stop",
				      g_variant_new ("(s)", "C.UTF-8"),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1, NULL, error);
	if (val == NULL)
		return FALSE;
	g_clear_pointer (&priv->address, g_free);
	return TRUE;
}

static gboolean
_client_subprocess_wait_check (GSubprocess *subprocess, GError **error)
{
	gint rc;
	if (!g_subprocess_wait (subprocess, NULL, error)) {
		g_prefix_error (error, "failed to run pkexec: ");
		return FALSE;
	}
	rc = g_subprocess_get_exit_status (subprocess);
	if (rc != 0) {
		GInputStream *istream = g_subprocess_get_stderr_pipe (subprocess);
		gchar buf[1024] = { 0x0 };
		gsize sz = 0;
		g_input_stream_read_all (istream, buf, sizeof(buf) - 1, &sz, NULL, NULL);
		if (sz == 0) {
			g_set_error_literal (error, G_IO_ERROR, rc,
					     "Failed to run helper without stderr");
			return FALSE;
		}
		g_set_error_literal (error, G_IO_ERROR, rc, buf);
		return FALSE;
	}
	return TRUE;
}

typedef enum {
	_NOTIFY_EXPIRED,
	_NOTIFY_REGISTRATION_REQUIRED,
	_NOTIFY_REGISTERED
} _NotifyKind;

static void
_show_notification (GsdSubscriptionManager *manager, _NotifyKind notify_kind)
{
	GsdSubscriptionManagerPrivate *priv = manager->priv;
	switch (notify_kind) {
	case _NOTIFY_EXPIRED:
		notify_notification_close (priv->notification_registered, NULL);
		notify_notification_close (priv->notification_registration_required, NULL);
		notify_notification_show (priv->notification_expired, NULL);
		break;
	case _NOTIFY_REGISTRATION_REQUIRED:
		notify_notification_close (priv->notification_registered, NULL);
		notify_notification_close (priv->notification_expired, NULL);
		notify_notification_show (priv->notification_registration_required, NULL);
		break;
	case _NOTIFY_REGISTERED:
		notify_notification_close (priv->notification_expired, NULL);
		notify_notification_close (priv->notification_registration_required, NULL);
		notify_notification_show (priv->notification_registered, NULL);
		break;
	default:
		break;
	}
	g_timer_reset (priv->timer_last_notified);
}

static void
_client_maybe__show_notification (GsdSubscriptionManager *manager)
{
	GsdSubscriptionManagerPrivate *priv = manager->priv;

	/* startup */
	if (priv->subscription_status_last == GSD_SUBMAN_SUBSCRIPTION_STATUS_UNKNOWN &&
	    priv->subscription_status == GSD_SUBMAN_SUBSCRIPTION_STATUS_UNKNOWN) {
		_show_notification (manager, _NOTIFY_REGISTRATION_REQUIRED);
		return;
	}

	/* something changed */
	if (priv->subscription_status_last != priv->subscription_status) {
		g_debug ("transisition from subscription status '%s' to '%s'",
			 gsd_subman_subscription_status_to_string (priv->subscription_status_last),
			 gsd_subman_subscription_status_to_string (priv->subscription_status));

		/* needs registration */
		if (priv->subscription_status_last == GSD_SUBMAN_SUBSCRIPTION_STATUS_VALID &&
		    priv->subscription_status == GSD_SUBMAN_SUBSCRIPTION_STATUS_INVALID) {
			_show_notification (manager, _NOTIFY_REGISTRATION_REQUIRED);
			return;
		}

		/* was unregistered */
		if (priv->subscription_status_last == GSD_SUBMAN_SUBSCRIPTION_STATUS_VALID &&
		    priv->subscription_status == GSD_SUBMAN_SUBSCRIPTION_STATUS_UNKNOWN) {
			_show_notification (manager, _NOTIFY_REGISTRATION_REQUIRED);
			return;
		}

		/* registered */
		if (priv->subscription_status_last == GSD_SUBMAN_SUBSCRIPTION_STATUS_UNKNOWN &&
		    priv->subscription_status == GSD_SUBMAN_SUBSCRIPTION_STATUS_VALID &&
		    g_timer_elapsed (priv->timer_last_notified, NULL) > 60) {
			_show_notification (manager, _NOTIFY_REGISTERED);
			return;
		}
	}

	/* nag again */
	if (priv->subscription_status == GSD_SUBMAN_SUBSCRIPTION_STATUS_UNKNOWN &&
	    g_timer_elapsed (priv->timer_last_notified, NULL) > 60 * 60 * 24) {
		_show_notification (manager, _NOTIFY_REGISTRATION_REQUIRED);
		return;
	}
	if (priv->subscription_status == GSD_SUBMAN_SUBSCRIPTION_STATUS_INVALID &&
	    g_timer_elapsed (priv->timer_last_notified, NULL) > 60 * 60 * 24) {
		_show_notification (manager, _NOTIFY_EXPIRED);
		return;
	}
	if (priv->subscription_status == GSD_SUBMAN_SUBSCRIPTION_STATUS_PARTIALLY_VALID &&
	    g_timer_elapsed (priv->timer_last_notified, NULL) > 60 * 60 * 24) {
		_show_notification (manager, _NOTIFY_EXPIRED);
		return;
	}
}

static gboolean
_client_register_with_keys (GsdSubscriptionManager *manager,
				  const gchar *hostname,
				  const gchar *organisation,
				  const gchar *activation_key,
				  GError **error)
{
	GsdSubscriptionManagerPrivate *priv = manager->priv;
	g_autoptr(GSubprocess) subprocess = NULL;

	/* apparently: "we can't send registration credentials over the regular
	 * system or session bus since those aren't really locked down..." */
	if (!_client_register_start (manager, error))
		return FALSE;
	g_debug ("spawning %s", LIBEXECDIR "/gsd-subman-helper");
	subprocess = g_subprocess_new (G_SUBPROCESS_FLAGS_STDERR_PIPE, error,
				       "pkexec", LIBEXECDIR "/gsd-subman-helper",
				       "--kind", "register-with-key",
				       "--address", priv->address,
				       "--hostname", hostname,
				       "--organisation", organisation,
				       "--activation-key", activation_key,
				       NULL);
	if (subprocess == NULL) {
		g_prefix_error (error, "failed to find pkexec: ");
		return FALSE;
	}
	if (!_client_subprocess_wait_check (subprocess, error))
		return FALSE;

	/* FIXME: also do on error? */
	if (!_client_register_stop (manager, error))
		return FALSE;
	if (!_client_subscription_status_update (manager, error))
		return FALSE;
	if (!_client_installed_products_update (manager, error))
		return FALSE;
	_client_maybe__show_notification (manager);

	/* success */
	return TRUE;
}

static gboolean
_client_register (GsdSubscriptionManager *manager,
			 const gchar *hostname,
			 const gchar *organisation,
			 const gchar *username,
			 const gchar *password,
			 GError **error)
{
	GsdSubscriptionManagerPrivate *priv = manager->priv;
	g_autoptr(GSubprocess) subprocess = NULL;

	/* fallback */
	if (organisation == NULL)
		organisation = "";

	/* apparently: "we can't send registration credentials over the regular
	 * system or session bus since those aren't really locked down..." */
	if (!_client_register_start (manager, error))
		return FALSE;
	g_debug ("spawning %s", LIBEXECDIR "/gsd-subman-helper");
	subprocess = g_subprocess_new (G_SUBPROCESS_FLAGS_STDERR_PIPE, error,
				       "pkexec", LIBEXECDIR "/gsd-subman-helper",
				       "--kind", "register-with-username",
				       "--address", priv->address,
				       "--hostname", hostname,
				       "--organisation", organisation,
				       "--username", username,
				       "--password", password,
				       NULL);
	if (subprocess == NULL) {
		g_prefix_error (error, "failed to find pkexec: ");
		return FALSE;
	}
	if (!_client_subprocess_wait_check (subprocess, error))
		return FALSE;

	/* FIXME: also do on error? */
	if (!_client_register_stop (manager, error))
		return FALSE;
	if (!_client_subscription_status_update (manager, error))
		return FALSE;
	if (!_client_installed_products_update (manager, error))
		return FALSE;
	_client_maybe__show_notification (manager);
	return TRUE;
}

static gboolean
_client_unregister (GsdSubscriptionManager *manager, GError **error)
{
	g_autoptr(GSubprocess) subprocess = NULL;

	/* apparently: "we can't send registration credentials over the regular
	 * system or session bus since those aren't really locked down..." */
	if (!_client_register_start (manager, error))
		return FALSE;
	g_debug ("spawning %s", LIBEXECDIR "/gsd-subman-helper");
	subprocess = g_subprocess_new (G_SUBPROCESS_FLAGS_STDERR_PIPE, error,
				       "pkexec", LIBEXECDIR "/gsd-subman-helper",
				       "--kind", "unregister",
				       NULL);
	if (subprocess == NULL) {
		g_prefix_error (error, "failed to find pkexec: ");
		return FALSE;
	}
	if (!_client_subprocess_wait_check (subprocess, error))
		return FALSE;
	if (!_client_subscription_status_update (manager, error))
		return FALSE;
	if (!_client_installed_products_update (manager, error))
		return FALSE;
	_client_maybe__show_notification (manager);
	return TRUE;
}

static gboolean
_client_update_config (GsdSubscriptionManager *manager, GError **error)
{
	GsdSubscriptionManagerPrivate *priv = manager->priv;
	g_autoptr(GVariant) val = NULL;
	g_autoptr(GVariant) val_server = NULL;
	g_autoptr(GVariantDict) dict = NULL;
	GVariantIter iter;
	gchar *key;
	gchar *value;

	val = g_dbus_proxy_call_sync (priv->proxies[_RHSM_INTERFACE_CONFIG],
				      "GetAll",
				      g_variant_new ("(s)", "C.UTF-8"),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1, NULL, error);
	if (val == NULL)
		return FALSE;
	dict = g_variant_dict_new (g_variant_get_child_value (val, 0));
	val_server = g_variant_dict_lookup_value (dict, "server", G_VARIANT_TYPE("a{ss}"));
	if (val_server != NULL) {
		g_variant_iter_init (&iter, val_server);
		while (g_variant_iter_next (&iter, "{ss}", &key, &value)) {
			g_debug ("%s=%s", key, value);
			g_hash_table_insert (priv->config,
					     g_steal_pointer (&key),
					     g_steal_pointer (&value));
		}
	}
	return TRUE;
}

static void
_subman_proxy_signal_cb (GDBusProxy *proxy,
			 const gchar *sender_name,
			 const gchar *signal_name,
			 GVariant *parameters,
			 GsdSubscriptionManager *manager)
{
	g_autoptr(GError) error = NULL;
	if (!_client_syspurpose_update (manager, &error)) {
		g_warning ("failed to update syspurpose: %s", error->message);
		g_clear_error (&error);
	}
	if (!_client_subscription_status_update (manager, &error)) {
		g_warning ("failed to update subscription status: %s", error->message);
		g_clear_error (&error);
	}
	if (!_client_installed_products_update (manager, &error)) {
		g_warning ("failed to update installed products: %s", error->message);
		g_clear_error (&error);
	}
	_client_maybe__show_notification (manager);
}

static void
_client_unload (GsdSubscriptionManager *manager)
{
	GsdSubscriptionManagerPrivate *priv = manager->priv;
	for (guint i = 0; i < _RHSM_INTERFACE_LAST; i++)
		g_clear_object (&priv->proxies[i]);
	g_hash_table_unref (priv->config);
}

static const gchar *
_rhsm_interface_to_string (_RhsmInterface kind)
{
	if (kind == _RHSM_INTERFACE_CONFIG)
		return "Config";
	if (kind == _RHSM_INTERFACE_REGISTER_SERVER)
		return "RegisterServer";
	if (kind == _RHSM_INTERFACE_ATTACH)
		return "Attach";
	if (kind == _RHSM_INTERFACE_ENTITLEMENT)
		return "Entitlement";
	if (kind == _RHSM_INTERFACE_PRODUCTS)
		return "Products";
	if (kind == _RHSM_INTERFACE_CONSUMER)
		return "Consumer";
	if (kind == _RHSM_INTERFACE_SYSPURPOSE)
		return "Syspurpose";
	return NULL;
}

static gboolean
_client_load (GsdSubscriptionManager *manager, GError **error)
{
	GsdSubscriptionManagerPrivate *priv = manager->priv;

	priv->config = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	/* connect to all the interfaces on the *different* objects :| */
	for (guint i = 0; i < _RHSM_INTERFACE_LAST; i++) {
		const gchar *kind = _rhsm_interface_to_string (i);
		g_autofree gchar *opath = g_strdup_printf ("/com/redhat/RHSM1/%s", kind);
		g_autofree gchar *iface = g_strdup_printf ("com.redhat.RHSM1.%s", kind);
		priv->proxies[i] =
			g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
						       G_DBUS_PROXY_FLAGS_NONE,
						       NULL,
						       "com.redhat.RHSM1",
						       opath, iface,
						       NULL,
						       error);
		if (priv->proxies[i] == NULL)
			return FALSE;
		/* we want to get notified if the status of the system changes */
		g_signal_connect (priv->proxies[i], "g-signal",
				  G_CALLBACK (_subman_proxy_signal_cb), manager);
	}

	/* get initial status */
	if (!_client_update_config (manager, error))
		return FALSE;
	if (!_client_subscription_status_update (manager, error))
		return FALSE;
	if (!_client_installed_products_update (manager, error))
		return FALSE;
	if (!_client_syspurpose_update (manager, error))
		return FALSE;

	/* success */
	return TRUE;
}

gboolean
gsd_subscription_manager_start (GsdSubscriptionManager *manager, GError **error)
{
	gboolean ret;
	g_debug ("Starting subscription manager");
	gnome_settings_profile_start (NULL);
	ret = _client_load (manager, error);
	_client_maybe__show_notification (manager);
	gnome_settings_profile_end (NULL);
	return ret;
}

void
gsd_subscription_manager_stop (GsdSubscriptionManager *manager)
{
	g_debug ("Stopping subscription manager");
	_client_unload (manager);
}

static void
gsd_subscription_manager_class_init (GsdSubscriptionManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gsd_subscription_manager_finalize;
        notify_init ("gnome-settings-daemon");
	g_type_class_add_private (klass, sizeof (GsdSubscriptionManagerPrivate));
}

static void
_launch_info_overview (void)
{
	const gchar *argv[] = { "gnome-control-center", "info-overview", NULL };
	g_debug ("Running gnome-control-center info-overview");
	g_spawn_async (NULL, (gchar **) argv, NULL, G_SPAWN_SEARCH_PATH,
		       NULL, NULL, NULL, NULL);
}

static void
_notify_closed_cb (NotifyNotification *notification, gpointer user_data)
{
	/* FIXME: only launch when clicking on the main body, not the window close */
	if (notify_notification_get_closed_reason (notification) == 0x400)
		_launch_info_overview ();
}

static void
_notify_clicked_cb (NotifyNotification *notification, char *action, gpointer user_data)
{
	_launch_info_overview ();
}

static void
gsd_subscription_manager_init (GsdSubscriptionManager *manager)
{
	GsdSubscriptionManagerPrivate *priv = manager->priv = GSD_SUBSCRIPTION_MANAGER_GET_PRIVATE (manager);

	priv->installed_products = g_ptr_array_new_with_free_func ((GDestroyNotify) product_data_free);
	priv->timer_last_notified = g_timer_new ();

	/* expired */
	priv->notification_expired =
		notify_notification_new (_("Subscription Has Expired"),
					 _("Add or renew a subscription to continue receiving software updates."),
					 NULL);
	notify_notification_set_app_name (priv->notification_expired, _("Subscription"));
	notify_notification_set_hint_string (priv->notification_expired, "desktop-entry", "subman-panel");
	notify_notification_set_hint_string (priv->notification_expired, "x-gnome-privacy-scope", "system");
	notify_notification_set_urgency (priv->notification_expired, NOTIFY_URGENCY_CRITICAL);
	notify_notification_add_action (priv->notification_expired,
					"info-overview", _("Subscribe System…"),
					_notify_clicked_cb,
					manager, NULL);
	g_signal_connect (priv->notification_expired, "closed",
			  G_CALLBACK (_notify_closed_cb), manager);

	/* registered */
	priv->notification_registered =
		notify_notification_new (_("Registration Successful"),
					 _("The system has been registered and software updates have been enabled."),
					 NULL);
	notify_notification_set_app_name (priv->notification_registered, _("Subscription"));
	notify_notification_set_hint_string (priv->notification_registered, "desktop-entry", "subman-panel");
	notify_notification_set_hint_string (priv->notification_registered, "x-gnome-privacy-scope", "system");
	notify_notification_set_urgency (priv->notification_registered, NOTIFY_URGENCY_CRITICAL);
	g_signal_connect (priv->notification_registered, "closed",
			  G_CALLBACK (_notify_closed_cb), manager);

	/* registration required */
	priv->notification_registration_required =
		notify_notification_new (_("System Not Registered"),
					 _("Please register your system to receive software updates."),
					 NULL);
	notify_notification_set_app_name (priv->notification_registration_required, _("Subscription"));
	notify_notification_set_hint_string (priv->notification_registration_required, "desktop-entry", "subman-panel");
	notify_notification_set_hint_string (priv->notification_registration_required, "x-gnome-privacy-scope", "system");
	notify_notification_set_urgency (priv->notification_registration_required, NOTIFY_URGENCY_CRITICAL);
	notify_notification_add_action (priv->notification_registration_required,
					"info-overview", _("Register System…"),
					_notify_clicked_cb,
					manager, NULL);
	g_signal_connect (priv->notification_registration_required, "closed",
			  G_CALLBACK (_notify_closed_cb), manager);
}

static void
gsd_subscription_manager_finalize (GObject *object)
{
	GsdSubscriptionManager *manager;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GSD_IS_SUBSCRIPTION_MANAGER (object));

	manager = GSD_SUBSCRIPTION_MANAGER (object);

	gsd_subscription_manager_stop (manager);

	if (manager->priv->bus_cancellable != NULL) {
		g_cancellable_cancel (manager->priv->bus_cancellable);
		g_clear_object (&manager->priv->bus_cancellable);
	}

	g_clear_pointer (&manager->priv->installed_products, g_ptr_array_unref);
	g_clear_pointer (&manager->priv->introspection_data, g_dbus_node_info_unref);
	g_clear_object (&manager->priv->connection);
	g_clear_object (&manager->priv->notification_expired);
	g_clear_object (&manager->priv->notification_registered);
	g_timer_destroy (manager->priv->timer_last_notified);

	if (manager->priv->name_id != 0) {
		g_bus_unown_name (manager->priv->name_id);
		manager->priv->name_id = 0;
	}

	G_OBJECT_CLASS (gsd_subscription_manager_parent_class)->finalize (object);
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
	GsdSubscriptionManager *manager = GSD_SUBSCRIPTION_MANAGER (user_data);
	g_autoptr(GError) error = NULL;

	if (g_strcmp0 (method_name, "Register") == 0) {
		const gchar *organisation = NULL;
		const gchar *hostname = NULL;

		if (FALSE) {
			g_dbus_method_invocation_return_error_literal (invocation,
								       G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
								       "Cannot register at this time");

			return;
		}

		g_autoptr(GVariantDict) dict = g_variant_dict_new (g_variant_get_child_value (parameters, 0));

		const gchar *kind = NULL;
		if (!g_variant_dict_lookup (dict, "kind", "&s", &kind)) {
			g_dbus_method_invocation_return_error_literal (invocation,
								       G_IO_ERROR, G_IO_ERROR_FAILED,
								       "No kind specified");

			return;
		}
		if (g_strcmp0 (kind, "username") == 0) {
			const gchar *username = NULL;
			const gchar *password = NULL;
			g_variant_dict_lookup (dict, "hostname", "&s", &hostname);
			g_variant_dict_lookup (dict, "organisation", "&s", &organisation);
			g_variant_dict_lookup (dict, "username", "&s", &username);
			g_variant_dict_lookup (dict, "password", "&s", &password);
			if (!_client_register (manager,
						     hostname,
						     organisation,
						     username,
						     password,
						     &error)) {
				g_dbus_method_invocation_return_gerror (invocation, error);
				return;
			}
		} else if (g_strcmp0 (kind, "key") == 0) {
			const gchar *activation_key = NULL;
			g_variant_dict_lookup (dict, "hostname", "&s", &hostname);
			g_variant_dict_lookup (dict, "organisation", "&s", &organisation);
			g_variant_dict_lookup (dict, "activation-key", "&s", &activation_key);
			if (!_client_register_with_keys (manager,
							       hostname,
							       organisation,
							       activation_key,
							       &error)) {
				g_dbus_method_invocation_return_gerror (invocation, error);
				return;
			}
		} else {
			g_dbus_method_invocation_return_error_literal (invocation,
								       G_IO_ERROR, G_IO_ERROR_FAILED,
								       "Invalid kind specified");

			return;
		}
		g_dbus_method_invocation_return_value (invocation, NULL);
	} else if (g_strcmp0 (method_name, "Unregister") == 0) {
		if (!_client_unregister (manager, &error)) {
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}
		g_dbus_method_invocation_return_value (invocation, NULL);
	} else {
		g_assert_not_reached ();
	}
}

static GVariant *
handle_get_property (GDBusConnection *connection,
		     const gchar *sender,
		     const gchar *object_path,
		     const gchar *interface_name,
		     const gchar *property_name,
		     GError **error, gpointer user_data)
{
	GsdSubscriptionManager *manager = GSD_SUBSCRIPTION_MANAGER (user_data);
	GsdSubscriptionManagerPrivate *priv = manager->priv;

	if (g_strcmp0 (interface_name, GSD_SUBSCRIPTION_DBUS_INTERFACE) != 0) {
		g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
			     "No such interface: %s", interface_name);
		return NULL;
	}

	if (g_strcmp0 (property_name, "SubscriptionStatus") == 0)
		return g_variant_new_uint32 (priv->subscription_status);

	if (g_strcmp0 (property_name, "InstalledProducts") == 0)
		return _make_installed_products_variant (priv->installed_products);

	g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
		     "Failed to get property: %s", property_name);
	return NULL;
}

static gboolean
handle_set_property (GDBusConnection *connection,
		     const gchar *sender,
		     const gchar *object_path,
		     const gchar *interface_name,
		     const gchar *property_name,
		     GVariant *value,
		     GError **error, gpointer user_data)
{
	if (g_strcmp0 (interface_name, GSD_SUBSCRIPTION_DBUS_INTERFACE) != 0) {
		g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
			     "No such interface: %s", interface_name);
		return FALSE;
	}
	g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
		     "No such property: %s", property_name);
	return FALSE;
}

static const GDBusInterfaceVTable interface_vtable =
{
	handle_method_call,
	handle_get_property,
	handle_set_property
};

static void
name_lost_handler_cb (GDBusConnection *connection, const gchar *name, gpointer user_data)
{
	g_debug ("lost name, so exiting");
	gtk_main_quit ();
}

static void
on_bus_gotten (GObject *source_object, GAsyncResult *res, GsdSubscriptionManager *manager)
{
	GsdSubscriptionManagerPrivate *priv = manager->priv;
	GDBusConnection *connection;
	g_autoptr(GError) error = NULL;

	connection = g_bus_get_finish (res, &error);
	if (connection == NULL) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning ("Could not get session bus: %s", error->message);
		return;
	}

	priv->connection = connection;
	g_dbus_connection_register_object (connection,
					   GSD_SUBSCRIPTION_DBUS_PATH,
					   priv->introspection_data->interfaces[0],
					   &interface_vtable,
					   manager,
					   NULL,
					   NULL);
	priv->name_id = g_bus_own_name_on_connection (connection,
						      GSD_SUBSCRIPTION_DBUS_NAME,
						      G_BUS_NAME_OWNER_FLAGS_NONE,
						      NULL,
						      name_lost_handler_cb,
						      manager,
						      NULL);
}

static void
register_manager_dbus (GsdSubscriptionManager *manager)
{
	GsdSubscriptionManagerPrivate *priv = manager->priv;

	priv->introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
	g_assert (priv->introspection_data != NULL);
	priv->bus_cancellable = g_cancellable_new ();

	g_bus_get (G_BUS_TYPE_SESSION, priv->bus_cancellable,
		   (GAsyncReadyCallback) on_bus_gotten, manager);
}

GsdSubscriptionManager *
gsd_subscription_manager_new (void)
{
	if (manager_object != NULL) {
		g_object_ref (manager_object);
	} else {
		manager_object = g_object_new (GSD_TYPE_SUBSCRIPTION_MANAGER, NULL);
		g_object_add_weak_pointer (manager_object,
					   (gpointer *) &manager_object);
		register_manager_dbus (manager_object);
	}

	return GSD_SUBSCRIPTION_MANAGER (manager_object);
}
