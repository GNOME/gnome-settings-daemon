/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2019 Richard Hughes <rhughes@redhat.com>
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

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

#include <gio/gio.h>
#include <json-glib/json-glib.h>

#define DBUS_TIMEOUT 300000 /* 5 minutes */

static void
_helper_convert_error (const gchar *json_txt, GError **error)
{
	JsonNode *json_root;
	JsonObject *json_obj;
	const gchar *message;
	g_autoptr(JsonParser) json_parser = json_parser_new ();

	/* this may be plain text or JSON :| */
	if (!json_parser_load_from_data (json_parser, json_txt, -1, NULL)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     json_txt);
		return;
	}
	json_root = json_parser_get_root (json_parser);
	json_obj = json_node_get_object (json_root);
	if (!json_object_has_member (json_obj, "message")) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "no message' in %s", json_txt);
		return;
	}
	message = json_object_get_string_member (json_obj, "message");
	if (g_strstr_len (message, -1, "Invalid user credentials") != NULL) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_PERMISSION_DENIED,
				     message);
		return;
	}
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     message);
}

static gboolean
_helper_unregister (GError **error)
{
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GVariantBuilder) proxy_options = NULL;
	g_autoptr(GVariant) res = NULL;

	g_debug ("unregistering");
	proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
					       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
					       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
					       NULL,
					       "com.redhat.RHSM1",
					       "/com/redhat/RHSM1/Unregister",
					       "com.redhat.RHSM1.Unregister",
					       NULL, error);
	if (proxy == NULL) {
		g_prefix_error (error, "Failed to get proxy: ");
		return FALSE;
	}
	proxy_options = g_variant_builder_new (G_VARIANT_TYPE_VARDICT);
	res = g_dbus_proxy_call_sync (proxy,
				      "Unregister",
				      g_variant_new ("(a{sv}s)",
						     proxy_options,
						     ""), /* lang */
				      G_DBUS_CALL_FLAGS_NONE,
				      DBUS_TIMEOUT,
				      NULL, error);
	return res != NULL;
}

static gboolean
_helper_auto_attach (GError **error)
{
	const gchar *str = NULL;
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GVariantBuilder) proxy_options = NULL;
	g_autoptr(GVariant) res = NULL;

	g_debug ("auto-attaching subscriptions");
	proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
					       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
					       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
					       NULL,
					       "com.redhat.RHSM1",
					       "/com/redhat/RHSM1/Attach",
					       "com.redhat.RHSM1.Attach",
					       NULL, error);
	if (proxy == NULL) {
		g_prefix_error (error, "Failed to get proxy: ");
		return FALSE;
	}
	proxy_options = g_variant_builder_new (G_VARIANT_TYPE_VARDICT);
	res = g_dbus_proxy_call_sync (proxy,
				      "AutoAttach",
				      g_variant_new ("(sa{sv}s)",
						     "", /* now? */
						     proxy_options,
						     ""), /* lang */
				      G_DBUS_CALL_FLAGS_NONE,
				      DBUS_TIMEOUT,
				      NULL, error);
	if (res == NULL)
		return FALSE;
	g_variant_get (res, "(&s)", &str);
	g_debug ("Attach.AutoAttach: %s", str);
	return TRUE;
}

static gboolean
_helper_save_config (const gchar *key, const gchar *value, GError **error)
{
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GVariant) res = NULL;
	proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
					       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
					       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
					       NULL,
					       "com.redhat.RHSM1",
					       "/com/redhat/RHSM1/Config",
					       "com.redhat.RHSM1.Config",
					       NULL, error);
	if (proxy == NULL) {
		g_prefix_error (error, "Failed to get proxy: ");
		return FALSE;
	}
	res = g_dbus_proxy_call_sync (proxy, "Set",
				      g_variant_new ("(svs)",
						     key,
						     g_variant_new_string (value),
						     ""), /* lang */
				      G_DBUS_CALL_FLAGS_NONE,
				      DBUS_TIMEOUT,
				      NULL, error);
	return res != NULL;
}

int
main (int argc, char *argv[])
{
	const gchar *userlang = ""; /* as root, so no translations */
	g_autofree gchar *activation_key = NULL;
	g_autofree gchar *address = NULL;
	g_autofree gchar *hostname = NULL;
	g_autofree gchar *kind = NULL;
	g_autofree gchar *organisation = NULL;
	g_autofree gchar *password = NULL;
	g_autofree gchar *port = NULL;
	g_autofree gchar *prefix = NULL;
	g_autofree gchar *proxy_server = NULL;
	g_autofree gchar *username = NULL;
	g_autoptr(GDBusConnection) conn_private = NULL;
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) context = g_option_context_new (NULL);
	g_autoptr(GVariantBuilder) proxy_options = NULL;
	g_autoptr(GVariantBuilder) subman_conopts = NULL;
	g_autoptr(GVariantBuilder) subman_options = NULL;

	const GOptionEntry options[] = {
		{ "kind", '\0', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
			&kind, "Kind, e.g. 'username' or 'key'", NULL },
		{ "address", '\0', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
			&address, "UNIX address", NULL },
		{ "username", '\0', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
			&username, "Username", NULL },
		{ "password", '\0', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
			&password, "Password", NULL },
		{ "organisation", '\0', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
			&organisation, "Organisation", NULL },
		{ "activation-key", '\0', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
			&activation_key, "Activation keys", NULL },
		{ "hostname", '\0', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING,
			&hostname, "Registration server hostname", NULL },
		{ "prefix", '\0', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING,
			&prefix, "Registration server prefix", NULL },
		{ "port", '\0', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING,
			&port, "Registration server port", NULL },
		{ "proxy", '\0', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING,
			&proxy_server, "Proxy settings", NULL },
		{ NULL}
	};

	/* check calling UID */
	if (getuid () != 0 || geteuid () != 0) {
		g_printerr ("This program can only be used by the root user\n");
		return G_IO_ERROR_NOT_SUPPORTED;
	}
	g_option_context_add_main_entries (context, options, NULL);
	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_printerr ("Failed to parse arguments: %s\n", error->message);
		return G_IO_ERROR_NOT_SUPPORTED;
	}

	/* uncommon actions */
	if (kind == NULL) {
		g_printerr ("No --kind specified\n");
		return G_IO_ERROR_INVALID_DATA;
	}
	if (g_strcmp0 (kind, "unregister") == 0) {
		if (!_helper_unregister (&error)) {
			g_printerr ("Failed to Unregister: %s\n", error->message);
			return G_IO_ERROR_NOT_INITIALIZED;
		}
		return EXIT_SUCCESS;
	}
	if (g_strcmp0 (kind, "auto-attach") == 0) {
		if (!_helper_auto_attach (&error)) {
			g_printerr ("Failed to AutoAttach: %s\n", error->message);
			return G_IO_ERROR_NOT_INITIALIZED;
		}
		return EXIT_SUCCESS;
	}

	/* connect to abstract socket for reasons */
	if (address == NULL) {
		g_printerr ("No --address specified\n");
		return G_IO_ERROR_INVALID_DATA;
	}
	conn_private = g_dbus_connection_new_for_address_sync (address,
							       G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
							       NULL, NULL,
							       &error);
	if (conn_private == NULL) {
		g_printerr ("Invalid --address specified: %s\n", error->message);
		return G_IO_ERROR_INVALID_DATA;
	}
	proxy = g_dbus_proxy_new_sync (conn_private,
				       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
				       NULL, /* GDBusInterfaceInfo */
				       NULL, /* name */
				       "/com/redhat/RHSM1/Register",
				       "com.redhat.RHSM1.Register",
				       NULL, &error);
	if (proxy == NULL) {
		g_printerr ("Count not contact RHSM: %s\n", error->message);
		return G_IO_ERROR_NOT_FOUND;
	}

	/* no options */
	subman_options = g_variant_builder_new (G_VARIANT_TYPE("a{ss}"));

	/* set registration server */
	if (hostname == NULL || hostname[0] == '\0')
		hostname = g_strdup ("subscription.rhsm.redhat.com");
	if (prefix == NULL || prefix[0] == '\0')
		prefix = g_strdup ("/subscription");
	if (port == NULL || port[0] == '\0')
		port = g_strdup ("443");
	subman_conopts = g_variant_builder_new (G_VARIANT_TYPE("a{ss}"));
	g_variant_builder_add (subman_conopts, "{ss}", "host", hostname);
	g_variant_builder_add (subman_conopts, "{ss}", "handler", prefix);
	g_variant_builder_add (subman_conopts, "{ss}", "port", port);

	/* call into RHSM */
	if (g_strcmp0 (kind, "register-with-key") == 0) {
		g_auto(GStrv) activation_keys = NULL;
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GVariant) res = NULL;

		if (activation_key == NULL) {
			g_printerr ("Required --activation-key\n");
			return G_IO_ERROR_INVALID_DATA;
		}
		if (organisation == NULL) {
			g_printerr ("Required --organisation\n");
			return G_IO_ERROR_INVALID_DATA;
		}

		g_debug ("registering using activation key");
		activation_keys = g_strsplit (activation_key, ",", -1);
		res = g_dbus_proxy_call_sync (proxy,
					      "RegisterWithActivationKeys",
					      g_variant_new ("(s^asa{ss}a{ss}s)",
							     organisation,
							     activation_keys,
							     subman_options,
							     subman_conopts,
							     userlang),
					      G_DBUS_CALL_FLAGS_NO_AUTO_START,
					      DBUS_TIMEOUT,
					      NULL, &error_local);
		if (res == NULL) {
			g_dbus_error_strip_remote_error (error_local);
			_helper_convert_error (error_local->message, &error);
			g_printerr ("Failed to RegisterWithActivationKeys: %s\n", error->message);
			return error->code;
		}
	} else if (g_strcmp0 (kind, "register-with-username") == 0) {
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GVariant) res = NULL;

		g_debug ("registering using username and password");
		if (username == NULL) {
			g_printerr ("Required --username\n");
			return G_IO_ERROR_INVALID_DATA;
		}
		if (password == NULL) {
			g_printerr ("Required --password\n");
			return G_IO_ERROR_INVALID_DATA;
		}
		if (organisation == NULL) {
			g_printerr ("Required --organisation\n");
			return G_IO_ERROR_INVALID_DATA;
		}
		res = g_dbus_proxy_call_sync (proxy,
					      "Register",
					      g_variant_new ("(sssa{ss}a{ss}s)",
							     organisation,
							     username,
							     password,
							     subman_options,
							     subman_conopts,
							     userlang),
					      G_DBUS_CALL_FLAGS_NO_AUTO_START,
					      DBUS_TIMEOUT,
					      NULL, &error_local);
		if (res == NULL) {
			g_dbus_error_strip_remote_error (error_local);
			_helper_convert_error (error_local->message, &error);
			g_printerr ("Failed to Register: %s\n", error->message);
			return error->code;
		}
	} else {
		g_printerr ("Invalid --kind specified: %s\n", kind);
		return G_IO_ERROR_INVALID_DATA;
	}

	/* set the new hostname */
	if (!_helper_save_config ("server.hostname", hostname, &error)) {
		g_printerr ("Failed to save hostname: %s\n", error->message);
		return G_IO_ERROR_NOT_INITIALIZED;
	}
	if (!_helper_save_config ("server.prefix", prefix, &error)) {
		g_printerr ("Failed to save prefix: %s\n", error->message);
		return G_IO_ERROR_NOT_INITIALIZED;
	}
	if (!_helper_save_config ("server.port", port, &error)) {
		g_printerr ("Failed to save port: %s\n", error->message);
		return G_IO_ERROR_NOT_INITIALIZED;
	}

	/* wait for rhsmd to notice the new config */
	g_usleep (G_USEC_PER_SEC * 5);

	/* auto-attach */
	if (!_helper_auto_attach (&error)) {
		g_printerr ("Failed to AutoAttach: %s\n", error->message);
		return G_IO_ERROR_NOT_INITIALIZED;
	}

	return EXIT_SUCCESS;
}
