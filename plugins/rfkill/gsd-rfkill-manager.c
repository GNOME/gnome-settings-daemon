/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2010,2011 Red Hat, Inc.
 *
 * Author: Bastien Nocera <hadess@hadess.net>
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

/* Test with:
 *    gdbus call \
 *        --session \
 *        --dest org.gnome.SettingsDaemon.Rfkill \
 *        --object-path /org/gnome/SettingsDaemon/Rfkill \
 *        --method org.freedesktop.DBus.Properties.Set \
 *        "org.gnome.SettingsDaemon.Rfkill" \
 *        "AirplaneMode" \
 *        "<true|false>"
 * and
 *    gdbus call \
 *        --session \
 *        --dest org.gnome.SettingsDaemon.Rfkill \
 *        --object-path /org/gnome/SettingsDaemon/Rfkill \
 *        --method org.freedesktop.DBus.Properties.Set \
 *        "org.gnome.SettingsDaemon.Rfkill" \
 *        "BluetoothAirplaneMode" \
 *        "<true|false>"
 *
 * and
 *    gdbus call \
 *        --session \
 *        --dest org.gnome.SettingsDaemon.Rfkill \
 *        --object-path /org/gnome/SettingsDaemon/Rfkill \
 *        --method org.freedesktop.DBus.Properties.Set \
 *        "org.gnome.SettingsDaemon.Rfkill" \
 *        "WwanAirplaneMode" \
 *        "<true|false>"
 */

#include "config.h"

#include <gio/gio.h>
#include <string.h>

#include "gnome-settings-profile.h"
#include "gsd-rfkill-manager.h"
#include "rfkill-glib.h"
#include "gnome-settings-bus.h"

struct _GsdRfkillManager
{
        GObject                  parent;

        GDBusNodeInfo           *introspection_data;
        guint                    name_id;
        GDBusConnection         *connection;
        GCancellable            *cancellable;

        CcRfkillGlib            *rfkill;
        GHashTable              *killswitches;
        GHashTable              *bt_killswitches;
        GHashTable              *wwan_killswitches;

        /* In addition to using the rfkill kernel subsystem
           (which is exposed by wlan, wimax, bluetooth, nfc,
           some platform drivers and some usb modems), we
           need to go through NetworkManager, which in turn
           will tell ModemManager to write the right commands
           in the USB bus to take external modems down, all
           from userspace.
        */
        GDBusProxy              *nm_client;
        gboolean                 wwan_enabled;
        GDBusObjectManager      *mm_client;
        gboolean                 wwan_interesting;

        GsdSessionManager       *session;
        GBinding                *rfkill_input_inhibit_binding;

        gchar                   *chassis_type;
};

#define GSD_DBUS_NAME "org.gnome.SettingsDaemon"
#define GSD_DBUS_PATH "/org/gnome/SettingsDaemon"
#define GSD_DBUS_BASE_INTERFACE "org.gnome.SettingsDaemon"

#define GSD_RFKILL_DBUS_NAME GSD_DBUS_NAME ".Rfkill"
#define GSD_RFKILL_DBUS_PATH GSD_DBUS_PATH "/Rfkill"

static const gchar introspection_xml[] =
"<node>"
"  <interface name='org.gnome.SettingsDaemon.Rfkill'>"
"    <annotation name='org.freedesktop.DBus.GLib.CSymbol' value='gsd_rfkill_manager'/>"
"    <property name='AirplaneMode' type='b' access='readwrite'/>"
"    <property name='HardwareAirplaneMode' type='b' access='read'/>"
"    <property name='HasAirplaneMode' type='b' access='read'/>"
"    <property name='ShouldShowAirplaneMode' type='b' access='read'/>"
"    <property name='BluetoothAirplaneMode' type='b' access='readwrite'/>"
"    <property name='BluetoothHardwareAirplaneMode' type='b' access='read'/>"
"    <property name='BluetoothHasAirplaneMode' type='b' access='read'/>"
"    <property name='WwanAirplaneMode' type='b' access='readwrite'/>"
"    <property name='WwanHardwareAirplaneMode' type='b' access='read'/>"
"    <property name='WwanHasAirplaneMode' type='b' access='read'/>"
"  </interface>"
"</node>";

static void     gsd_rfkill_manager_class_init  (GsdRfkillManagerClass *klass);
static void     gsd_rfkill_manager_init        (GsdRfkillManager      *rfkill_manager);
static void     gsd_rfkill_manager_finalize    (GObject                    *object);

G_DEFINE_TYPE (GsdRfkillManager, gsd_rfkill_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static void
gsd_rfkill_manager_class_init (GsdRfkillManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_rfkill_manager_finalize;
}

static void
gsd_rfkill_manager_init (GsdRfkillManager *manager)
{
}

static gboolean
engine_get_airplane_mode_helper (GHashTable *killswitches)
{
	GHashTableIter iter;
	gpointer key, value;

	if (g_hash_table_size (killswitches) == 0)
		return FALSE;

	g_hash_table_iter_init (&iter, killswitches);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		int state;

		state = GPOINTER_TO_INT (value);

		/* A single rfkill switch that's unblocked? Airplane mode is off */
		if (state == RFKILL_STATE_UNBLOCKED)
			return FALSE;
	}

	return TRUE;
}

static gboolean
engine_get_bluetooth_airplane_mode (GsdRfkillManager *manager)
{
	return engine_get_airplane_mode_helper (manager->bt_killswitches);
}

static gboolean
engine_get_bluetooth_hardware_airplane_mode (GsdRfkillManager *manager)
{
	GHashTableIter iter;
	gpointer key, value;

	/* If we have no killswitches, hw airplane mode is off. */
	if (g_hash_table_size (manager->bt_killswitches) == 0)
		return FALSE;

	g_hash_table_iter_init (&iter, manager->bt_killswitches);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		int state;

		state = GPOINTER_TO_INT (value);

		/* A single rfkill switch that's not hw blocked? Hw airplane mode is off */
		if (state != RFKILL_STATE_HARD_BLOCKED) {
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
engine_get_has_bluetooth_airplane_mode (GsdRfkillManager *manager)
{
	return (g_hash_table_size (manager->bt_killswitches) > 0);
}

static gboolean
engine_get_wwan_airplane_mode (GsdRfkillManager *manager)
{
        gboolean is_airplane;

        is_airplane = engine_get_airplane_mode_helper (manager->wwan_killswitches);

        /* Try our luck with Modem Manager too.  Check only if no rfkill
         * devices found, or if rfkill reports all devices to be down.
         * (Airplane mode will be disabled if at least one device is up,
         * so if rfkill says no device is up, check any device is up via
         * Network Manager (which in turn, is handled via Modem Manager))
         */
        if (g_hash_table_size (manager->wwan_killswitches) == 0 || is_airplane)
                if (manager->wwan_interesting)
                        is_airplane = !manager->wwan_enabled;

        return is_airplane;
}

static gboolean
engine_get_wwan_hardware_airplane_mode (GsdRfkillManager *manager)
{
        GHashTableIter iter;
        gpointer key, value;

        /* If we have no killswitches, hw airplane mode is off. */
        if (g_hash_table_size (manager->wwan_killswitches) == 0)
                return FALSE;

        g_hash_table_iter_init (&iter, manager->wwan_killswitches);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                int state;

                state = GPOINTER_TO_INT (value);

                /* A single rfkill switch that's not hw blocked? Hw airplane mode is off */
                if (state != RFKILL_STATE_HARD_BLOCKED) {
                        return FALSE;
                }
        }

        return TRUE;
}

static gboolean
engine_get_has_wwan_airplane_mode (GsdRfkillManager *manager)
{
        return (g_hash_table_size (manager->wwan_killswitches) > 0 ||
                manager->wwan_interesting);
}

static gboolean
engine_get_airplane_mode (GsdRfkillManager *manager)
{
	if (!manager->wwan_interesting)
		return engine_get_airplane_mode_helper (manager->killswitches);
        /* wwan enabled? then airplane mode is off (because an USB modem
           could be on in this state) */
	return engine_get_airplane_mode_helper (manager->killswitches) && !manager->wwan_enabled;
}

static gboolean
engine_get_hardware_airplane_mode (GsdRfkillManager *manager)
{
	GHashTableIter iter;
	gpointer key, value;

        /* If we have no killswitches, hw airplane mode is off. */
        if (g_hash_table_size (manager->killswitches) == 0)
                return FALSE;

	g_hash_table_iter_init (&iter, manager->killswitches);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		int state;

		state = GPOINTER_TO_INT (value);

		/* A single rfkill switch that's not hw blocked? Hw airplane mode is off */
		if (state != RFKILL_STATE_HARD_BLOCKED) {
                        return FALSE;
                }
	}

        return TRUE;
}

static gboolean
engine_get_has_airplane_mode (GsdRfkillManager *manager)
{
        return (g_hash_table_size (manager->killswitches) > 0) ||
                manager->wwan_interesting;
}

static gboolean
engine_get_should_show_airplane_mode (GsdRfkillManager *manager)
{
        return (g_strcmp0 (manager->chassis_type, "desktop") != 0) &&
                (g_strcmp0 (manager->chassis_type, "server") != 0) &&
                (g_strcmp0 (manager->chassis_type, "vm") != 0) &&
                (g_strcmp0 (manager->chassis_type, "container") != 0);
}

static void
engine_properties_changed (GsdRfkillManager *manager)
{
        GVariantBuilder props_builder;
        GVariant *props_changed = NULL;

        /* not yet connected to the session bus */
        if (manager->connection == NULL)
                return;

        g_variant_builder_init (&props_builder, G_VARIANT_TYPE ("a{sv}"));

        g_variant_builder_add (&props_builder, "{sv}", "AirplaneMode",
                               g_variant_new_boolean (engine_get_airplane_mode (manager)));
        g_variant_builder_add (&props_builder, "{sv}", "HardwareAirplaneMode",
                               g_variant_new_boolean (engine_get_hardware_airplane_mode (manager)));
        g_variant_builder_add (&props_builder, "{sv}", "HasAirplaneMode",
                               g_variant_new_boolean (engine_get_has_airplane_mode (manager)));
        g_variant_builder_add (&props_builder, "{sv}", "ShouldShowAirplaneMode",
                               g_variant_new_boolean (engine_get_should_show_airplane_mode (manager)));
        g_variant_builder_add (&props_builder, "{sv}", "BluetoothAirplaneMode",
                               g_variant_new_boolean (engine_get_bluetooth_airplane_mode (manager)));
        g_variant_builder_add (&props_builder, "{sv}", "BluetoothHardwareAirplaneMode",
                               g_variant_new_boolean (engine_get_bluetooth_hardware_airplane_mode (manager)));
        g_variant_builder_add (&props_builder, "{sv}", "BluetoothHasAirplaneMode",
                               g_variant_new_boolean (engine_get_has_bluetooth_airplane_mode (manager)));
        g_variant_builder_add (&props_builder, "{sv}", "WwanAirplaneMode",
                               g_variant_new_boolean (engine_get_wwan_airplane_mode (manager)));
        g_variant_builder_add (&props_builder, "{sv}", "WwanHardwareAirplaneMode",
                               g_variant_new_boolean (engine_get_wwan_hardware_airplane_mode (manager)));
        g_variant_builder_add (&props_builder, "{sv}", "WwanHasAirplaneMode",
                               g_variant_new_boolean (engine_get_has_wwan_airplane_mode (manager)));

        props_changed = g_variant_new ("(s@a{sv}@as)", GSD_RFKILL_DBUS_NAME,
                                       g_variant_builder_end (&props_builder),
                                       g_variant_new_strv (NULL, 0));

        g_dbus_connection_emit_signal (manager->connection,
                                       NULL,
                                       GSD_RFKILL_DBUS_PATH,
                                       "org.freedesktop.DBus.Properties",
                                       "PropertiesChanged",
                                       props_changed, NULL);
}

static void
rfkill_changed (CcRfkillGlib     *rfkill,
		GList            *events,
		GsdRfkillManager  *manager)
{
	GList *l;
        int value;

	for (l = events; l != NULL; l = l->next) {
		struct rfkill_event *event = l->data;
                const gchar *type = "";

                if (event->type == RFKILL_TYPE_BLUETOOTH)
                        type = "Bluetooth";
                else if (event->type == RFKILL_TYPE_WWAN)
                        type = "WWAN";

                switch (event->op) {
                case RFKILL_OP_ADD:
                case RFKILL_OP_CHANGE:
                        if (event->hard)
                                value = RFKILL_STATE_HARD_BLOCKED;
                        else if (event->soft)
                                value = RFKILL_STATE_SOFT_BLOCKED;
                        else
                                value = RFKILL_STATE_UNBLOCKED;

                        g_hash_table_insert (manager->killswitches,
                                             GINT_TO_POINTER (event->idx),
                                             GINT_TO_POINTER (value));
                        if (event->type == RFKILL_TYPE_BLUETOOTH)
				g_hash_table_insert (manager->bt_killswitches,
						     GINT_TO_POINTER (event->idx),
						     GINT_TO_POINTER (value));
                        else if (event->type == RFKILL_TYPE_WWAN)
                                g_hash_table_insert (manager->wwan_killswitches,
                                                     GINT_TO_POINTER (event->idx),
                                                     GINT_TO_POINTER (value));
                        g_debug ("%s %srfkill with ID %d",
                                 event->op == RFKILL_OP_ADD ? "Added" : "Changed",
                                 type, event->idx);
                        break;
                case RFKILL_OP_DEL:
			g_hash_table_remove (manager->killswitches,
					     GINT_TO_POINTER (event->idx));
			if (event->type == RFKILL_TYPE_BLUETOOTH)
				g_hash_table_remove (manager->bt_killswitches,
						     GINT_TO_POINTER (event->idx));
                        else if (event->type == RFKILL_TYPE_WWAN)
                                g_hash_table_remove (manager->wwan_killswitches,
                                                     GINT_TO_POINTER (event->idx));
                        g_debug ("Removed %srfkill with ID %d", type, event->idx);
                        break;
                }
	}

        engine_properties_changed (manager);
}

static void
rfkill_set_cb (GObject      *source_object,
	       GAsyncResult *res,
	       gpointer      user_data)
{
	gboolean ret;
	GError *error = NULL;

	ret = cc_rfkill_glib_send_change_all_event_finish (CC_RFKILL_GLIB (source_object), res, &error);
	if (!ret) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT))
			g_debug ("Timed out waiting for blocked rfkills");
		else if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning ("Failed to set RFKill: %s", error->message);
		g_error_free (error);
	}
}

static void
set_wwan_complete (GObject      *object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
        GError *error;
        GVariant *variant;

        error = NULL;
        variant = g_dbus_proxy_call_finish (G_DBUS_PROXY (object), result, &error);

        if (variant == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Failed to set WWAN power status: %s", error->message);

                g_error_free (error);
        } else {
                g_variant_unref (variant);
        }
}

static gboolean
engine_set_bluetooth_airplane_mode (GsdRfkillManager *manager,
                                    gboolean          enable)
{
        cc_rfkill_glib_send_change_all_event (manager->rfkill, RFKILL_TYPE_BLUETOOTH,
                                              enable, manager->cancellable, rfkill_set_cb, manager);

        return TRUE;
}

static gboolean
engine_set_wwan_airplane_mode (GsdRfkillManager *manager,
                               gboolean          enable)
{
        cc_rfkill_glib_send_change_all_event (manager->rfkill, RFKILL_TYPE_WWAN,
                                              enable, manager->cancellable, rfkill_set_cb, manager);

        if (manager->nm_client) {
                g_dbus_proxy_call (manager->nm_client,
                                   "org.freedesktop.DBus.Properties.Set",
                                   g_variant_new ("(ssv)",
                                                  "org.freedesktop.NetworkManager",
                                                  "WwanEnabled",
                                                  g_variant_new_boolean (!enable)),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1, /* timeout */
                                   manager->cancellable,
                                   set_wwan_complete, NULL);
        }

        return TRUE;
}

static gboolean
engine_set_airplane_mode (GsdRfkillManager *manager,
                          gboolean          enable)
{
        cc_rfkill_glib_send_change_all_event (manager->rfkill, RFKILL_TYPE_ALL,
                                              enable, manager->cancellable, rfkill_set_cb, manager);

        /* Note: we set the the NM property even if there are no modems, so we don't
           need to resync when one is plugged in */
        if (manager->nm_client) {
                g_dbus_proxy_call (manager->nm_client,
                                   "org.freedesktop.DBus.Properties.Set",
                                   g_variant_new ("(ssv)",
                                                  "org.freedesktop.NetworkManager",
                                                  "WwanEnabled",
                                                  g_variant_new_boolean (!enable)),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1, /* timeout */
                                   manager->cancellable,
                                   set_wwan_complete, NULL);
        }

	return TRUE;
}

static gboolean
handle_set_property (GDBusConnection *connection,
                     const gchar     *sender,
                     const gchar     *object_path,
                     const gchar     *interface_name,
                     const gchar     *property_name,
                     GVariant        *value,
                     GError         **error,
                     gpointer         user_data)
{
        GsdRfkillManager *manager = GSD_RFKILL_MANAGER (user_data);

        if (g_strcmp0 (property_name, "AirplaneMode") == 0) {
                gboolean airplane_mode;
                g_variant_get (value, "b", &airplane_mode);
                return engine_set_airplane_mode (manager, airplane_mode);
        } else if (g_strcmp0 (property_name, "BluetoothAirplaneMode") == 0) {
                gboolean airplane_mode;
                g_variant_get (value, "b", &airplane_mode);
                return engine_set_bluetooth_airplane_mode (manager, airplane_mode);
        }

        if (g_strcmp0 (property_name, "WwanAirplaneMode") == 0) {
                gboolean airplane_mode;
                g_variant_get (value, "b", &airplane_mode);
                return engine_set_wwan_airplane_mode (manager, airplane_mode);
        }

        return FALSE;
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
        GsdRfkillManager *manager = GSD_RFKILL_MANAGER (user_data);

        /* Check session pointer as a proxy for whether the manager is in the
           start or stop state */
        if (manager->connection == NULL) {
                return NULL;
        }

        if (g_strcmp0 (property_name, "AirplaneMode") == 0) {
                gboolean airplane_mode;
                airplane_mode = engine_get_airplane_mode (manager);
                return g_variant_new_boolean (airplane_mode);
        }

        if (g_strcmp0 (property_name, "HardwareAirplaneMode") == 0) {
                gboolean hw_airplane_mode;
                hw_airplane_mode = engine_get_hardware_airplane_mode (manager);
                return g_variant_new_boolean (hw_airplane_mode);
        }

        if (g_strcmp0 (property_name, "ShouldShowAirplaneMode") == 0) {
                gboolean should_show_airplane_mode;
                should_show_airplane_mode = engine_get_should_show_airplane_mode (manager);
                return g_variant_new_boolean (should_show_airplane_mode);
        }

        if (g_strcmp0 (property_name, "HasAirplaneMode") == 0) {
                gboolean has_airplane_mode;
                has_airplane_mode = engine_get_has_airplane_mode (manager);
                return g_variant_new_boolean (has_airplane_mode);
        }

        if (g_strcmp0 (property_name, "BluetoothAirplaneMode") == 0) {
                gboolean airplane_mode;
                airplane_mode = engine_get_bluetooth_airplane_mode (manager);
                return g_variant_new_boolean (airplane_mode);
        }

        if (g_strcmp0 (property_name, "BluetoothHardwareAirplaneMode") == 0) {
                gboolean hw_airplane_mode;
                hw_airplane_mode = engine_get_bluetooth_hardware_airplane_mode (manager);
                return g_variant_new_boolean (hw_airplane_mode);
        }

        if (g_strcmp0 (property_name, "BluetoothHasAirplaneMode") == 0) {
                gboolean has_airplane_mode;
                has_airplane_mode = engine_get_has_bluetooth_airplane_mode (manager);
                return g_variant_new_boolean (has_airplane_mode);
        }

        if (g_strcmp0 (property_name, "WwanAirplaneMode") == 0) {
                gboolean airplane_mode;
                airplane_mode = engine_get_wwan_airplane_mode (manager);
                return g_variant_new_boolean (airplane_mode);
        }

        if (g_strcmp0 (property_name, "WwanHardwareAirplaneMode") == 0) {
                gboolean hw_airplane_mode;
                hw_airplane_mode = engine_get_wwan_hardware_airplane_mode (manager);
                return g_variant_new_boolean (hw_airplane_mode);
        }

        if (g_strcmp0 (property_name, "WwanHasAirplaneMode") == 0) {
                gboolean has_airplane_mode;
                has_airplane_mode = engine_get_has_wwan_airplane_mode (manager);
                return g_variant_new_boolean (has_airplane_mode);
        }

        return NULL;
}

static const GDBusInterfaceVTable interface_vtable =
{
        NULL,
        handle_get_property,
        handle_set_property
};

static void
on_bus_gotten (GObject               *source_object,
               GAsyncResult          *res,
               GsdRfkillManager *manager)
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
                                           GSD_RFKILL_DBUS_PATH,
                                           manager->introspection_data->interfaces[0],
                                           &interface_vtable,
                                           manager,
                                           NULL,
                                           NULL);

        manager->name_id = g_bus_own_name_on_connection (connection,
                                                               GSD_RFKILL_DBUS_NAME,
                                                               G_BUS_NAME_OWNER_FLAGS_NONE,
                                                               NULL,
                                                               NULL,
                                                               NULL,
                                                               NULL);

        manager->session = gnome_settings_bus_get_session_proxy ();
        manager->rfkill_input_inhibit_binding = g_object_bind_property (manager->session, "session-is-active",
                                                                              manager->rfkill, "rfkill-input-inhibited",
                                                                              G_BINDING_SYNC_CREATE);
}

static void
sync_wwan_enabled (GsdRfkillManager *manager)
{
        GVariant *property;

        property = g_dbus_proxy_get_cached_property (manager->nm_client,
                                                     "WwanEnabled");

        if (property == NULL) {
                /* GDBus telling us NM went down */
                return;
        }

        manager->wwan_enabled = g_variant_get_boolean (property);
        engine_properties_changed (manager);

        g_variant_unref (property);
}

static void
nm_signal (GDBusProxy *proxy,
           char       *sender_name,
           char       *signal_name,
           GVariant   *parameters,
           gpointer    user_data)
{
        GsdRfkillManager *manager = user_data;
        GVariant *changed;
        GVariant *property;

        if (g_strcmp0 (signal_name, "PropertiesChanged") == 0) {
                changed = g_variant_get_child_value (parameters, 0);
                property = g_variant_lookup_value (changed, "WwanEnabled", G_VARIANT_TYPE ("b"));
                g_dbus_proxy_set_cached_property (proxy, "WwanEnabled", property);

                if (property != NULL) {
                        sync_wwan_enabled (manager);
                        g_variant_unref (property);
                }

                g_variant_unref (changed);
        }
}

static void
on_nm_proxy_gotten (GObject      *source,
                    GAsyncResult *result,
                    gpointer      user_data)
{
        GsdRfkillManager *manager = user_data;
        GDBusProxy *proxy;
        GError *error;

        error = NULL;
        proxy = g_dbus_proxy_new_for_bus_finish (result, &error);

        if (proxy == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
                    !g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN))
                        g_warning ("Failed to acquire NetworkManager proxy: %s", error->message);

                g_error_free (error);
                goto out;
        }

        manager->nm_client = proxy;

        g_signal_connect (manager->nm_client, "g-signal",
                          G_CALLBACK (nm_signal), manager);
        sync_wwan_enabled (manager);

 out:
        g_object_unref (manager);
}

static void
sync_wwan_interesting (GDBusObjectManager *object_manager,
                       GDBusObject        *object,
                       GDBusInterface     *interface,
                       gpointer            user_data)
{
        GsdRfkillManager *manager = user_data;
        GList *objects;

        objects = g_dbus_object_manager_get_objects (object_manager);
        manager->wwan_interesting = (objects != NULL);
        engine_properties_changed (manager);

        g_list_free_full (objects, g_object_unref);
}

static void
on_mm_proxy_gotten (GObject      *source,
                    GAsyncResult *result,
                    gpointer      user_data)
{
        GsdRfkillManager *manager = user_data;
        GDBusObjectManager *proxy;
        GError *error;

        error = NULL;
        proxy = g_dbus_object_manager_client_new_for_bus_finish (result, &error);

        if (proxy == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
                    !g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN))
                        g_warning ("Failed to acquire ModemManager proxy: %s", error->message);

                g_error_free (error);
                goto out;
        }

        manager->mm_client = proxy;

        g_signal_connect (manager->mm_client, "interface-added",
                          G_CALLBACK (sync_wwan_interesting), manager);
        g_signal_connect (manager->mm_client, "interface-removed",
                          G_CALLBACK (sync_wwan_interesting), manager);
        sync_wwan_interesting (manager->mm_client, NULL, NULL, manager);

 out:
        g_object_unref (manager);
}

gboolean
gsd_rfkill_manager_start (GsdRfkillManager *manager,
                         GError         **error)
{
        g_autoptr(GError) local_error = NULL;

        gnome_settings_profile_start (NULL);

        manager->introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
        g_assert (manager->introspection_data != NULL);

        manager->killswitches = g_hash_table_new (g_direct_hash, g_direct_equal);
        manager->bt_killswitches = g_hash_table_new (g_direct_hash, g_direct_equal);
        manager->wwan_killswitches = g_hash_table_new (g_direct_hash, g_direct_equal);
        manager->rfkill = cc_rfkill_glib_new ();
        g_signal_connect (G_OBJECT (manager->rfkill), "changed",
                          G_CALLBACK (rfkill_changed), manager);

        if (!cc_rfkill_glib_open (manager->rfkill, &local_error)) {
                g_warning ("Error setting up rfkill: %s", local_error->message);
                g_clear_error (&local_error);
        }

        manager->cancellable = g_cancellable_new ();

        manager->chassis_type = gnome_settings_get_chassis_type ();

        g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                  G_DBUS_PROXY_FLAGS_NONE,
                                  NULL, /* g-interface-info */
                                  "org.freedesktop.NetworkManager",
                                  "/org/freedesktop/NetworkManager",
                                  "org.freedesktop.NetworkManager",
                                  manager->cancellable,
                                  on_nm_proxy_gotten, g_object_ref (manager));

        g_dbus_object_manager_client_new_for_bus (G_BUS_TYPE_SYSTEM,
                                                  G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START,
                                                  "org.freedesktop.ModemManager1",
                                                  "/org/freedesktop/ModemManager1",
                                                  NULL, NULL, NULL, /* get_proxy_type and closure */
                                                  manager->cancellable,
                                                  on_mm_proxy_gotten, g_object_ref (manager));

        /* Start process of owning a D-Bus name */
        g_bus_get (G_BUS_TYPE_SESSION,
                   manager->cancellable,
                   (GAsyncReadyCallback) on_bus_gotten,
                   manager);

        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_rfkill_manager_stop (GsdRfkillManager *manager)
{
        g_debug ("Stopping rfkill manager");

        if (manager->name_id != 0) {
                g_bus_unown_name (manager->name_id);
                manager->name_id = 0;
        }

        g_clear_pointer (&manager->introspection_data, g_dbus_node_info_unref);
        g_clear_object (&manager->connection);
        g_clear_object (&manager->rfkill_input_inhibit_binding);
        g_clear_object (&manager->session);
        g_clear_object (&manager->rfkill);
        g_clear_pointer (&manager->killswitches, g_hash_table_destroy);
        g_clear_pointer (&manager->bt_killswitches, g_hash_table_destroy);
        g_clear_pointer (&manager->wwan_killswitches, g_hash_table_destroy);

        if (manager->cancellable) {
                g_cancellable_cancel (manager->cancellable);
                g_clear_object (&manager->cancellable);
        }

        g_clear_object (&manager->nm_client);
        g_clear_object (&manager->mm_client);
        manager->wwan_enabled = FALSE;
        manager->wwan_interesting = FALSE;

        g_clear_pointer (&manager->chassis_type, g_free);
}

static void
gsd_rfkill_manager_finalize (GObject *object)
{
        GsdRfkillManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_RFKILL_MANAGER (object));

        manager = GSD_RFKILL_MANAGER (object);

        g_return_if_fail (manager != NULL);

        gsd_rfkill_manager_stop (manager);

        G_OBJECT_CLASS (gsd_rfkill_manager_parent_class)->finalize (object);
}

GsdRfkillManager *
gsd_rfkill_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_RFKILL_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_RFKILL_MANAGER (manager_object);
}
