/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Ludovico de Nittis <denittis@gnome.org>
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

#include <gio/gio.h>
#include <string.h>
#include <locale.h>

#include <glib-object.h>

#include "gnome-settings-profile.h"
#include "gsd-usbprotection-manager.h"

#define PRIVACY_SETTINGS "org.gnome.desktop.privacy"
#define USBGUARD "usbguard"
#define USBGUARD_CONTROL "usbguard-control"

#define USBGUARD_DBUS_NAME "org.usbguard"
#define USBGUARD_DBUS_PATH "/org/usbguard"
#define USBGUARD_DBUS_INTERFACE "org.usbguard"

#define USBGUARD_DBUS_PATH_POLICY USBGUARD_DBUS_PATH "/Policy"
#define USBGUARD_DBUS_INTERFACE_POLICY USBGUARD_DBUS_INTERFACE ".Policy"

#define APPLY_POLICY "apply-policy"
#define BLOCK "block"
#define REJECT "reject"

#define INSERTED_DEVICE_POLICY "InsertedDevicePolicy"
#define APPEND_RULE "appendRule"
#define ALLOW_ALL "allow id *:*"

struct GsdUSBProtectionManagerPrivate
{
        guint         start_idle_id;
        GSettings    *settings;
        GDBusProxy   *usbprotection;
        GCancellable *cancellable;
};

enum {
        NEVER,
        WITH_LOCKSCREEN,
        ALWAYS
};

static void gsd_usbprotection_manager_class_init (GsdUSBProtectionManagerClass *klass);
static void gsd_usbprotection_manager_init       (GsdUSBProtectionManager      *usbprotection_manager);
static void gsd_usbprotection_manager_finalize   (GObject                      *object);

G_DEFINE_TYPE_WITH_PRIVATE (GsdUSBProtectionManager, gsd_usbprotection_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static void
add_usbguard_allow_rule ()
{
        GDBusConnection *bus;
        GVariant *params;

        /* This prepends an "allow all" rule */
        params = g_variant_new ("(su)", ALLOW_ALL, 0);
        bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL);
        g_dbus_connection_call (bus,
                                USBGUARD_DBUS_NAME,
                                USBGUARD_DBUS_PATH_POLICY,
                                USBGUARD_DBUS_INTERFACE_POLICY,
                                APPEND_RULE,
                                params,
                                NULL, 0, -1, NULL, NULL, NULL);
        g_object_unref (bus);
}

static gboolean
is_usbguard_allow_rule_present (GVariant *rules)
{
        GVariantIter *iter = NULL;
        gchar *value;
        guint number = 0;

        g_variant_get(rules, "a(us)", &iter);
        while (g_variant_iter_loop (iter, "(us)", &number, &value)) {
                if (g_strcmp0 (value, ALLOW_ALL) == 0)
                        return TRUE;
        }
        return FALSE;
}


static void
usbguard_listrules_cb (GObject      *source_object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
        GVariant *result, *rules;
        g_autoptr(GError) error = NULL;

        result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object),
                                           res,
                                           &error);
        g_object_unref(source_object);

        if (!result) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Failed fetch USBGuard rules list: %s", error->message);
                return;
        }

        rules = g_variant_get_child_value (result, 0);
        if (!is_usbguard_allow_rule_present (rules))
                add_usbguard_allow_rule ();

}


static void
settings_changed_callback (GSettings               *settings,
                           const char              *key,
                           GsdUSBProtectionManager *manager)
{
        gchar *value_usbguard;
        guint settings_usb_value;
        gboolean usbguard_controlled;
        GVariant *params;
        GDBusConnection *bus;

        /* We react only if one of the two USB related properties has been changed */
        if (g_strcmp0 (key, USBGUARD_CONTROL) != 0 && g_strcmp0 (key, USBGUARD) != 0)
                return;

        usbguard_controlled = g_settings_get_boolean (settings, USBGUARD_CONTROL);
        settings_usb_value = g_settings_get_uint (settings, USBGUARD);
        g_debug ("USBGuard control is currently %i with a protection level of %i",
                 usbguard_controlled, settings_usb_value);

        /* Only if we are entitled to handle USBGuard */
        if (usbguard_controlled) {
                value_usbguard = (settings_usb_value == ALWAYS) ? BLOCK : APPLY_POLICY;
                params = g_variant_new ("(ss)",
                                        INSERTED_DEVICE_POLICY,
                                        value_usbguard);

                g_dbus_proxy_call (manager->priv->usbprotection,
                                   "setParameter",
                                   params,
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   manager->priv->cancellable,
                                   NULL, NULL);

                /* If we are in "Never" or "When lockscreen is active" */
                if (settings_usb_value != ALWAYS) {
                        params = g_variant_new ("(s)", "");
                        bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL);
                        g_dbus_connection_call (bus,
                                                USBGUARD_DBUS_NAME,
                                                USBGUARD_DBUS_PATH_POLICY,
                                                USBGUARD_DBUS_INTERFACE_POLICY,
                                                "listRules",
                                                params,
                                                NULL, 0, -1,
                                                manager->priv->cancellable,
                                                usbguard_listrules_cb,
                                                manager);
                }
        }
}

static void update_usbprotection_store (GsdUSBProtectionManager *manager,
                                        GVariant                *parameter)
{
        gchar *key;
        gboolean usbguard_controlled;
        guint settings_usb;
        GSettings *settings = manager->priv->settings;

        usbguard_controlled = g_settings_get_boolean (settings, USBGUARD_CONTROL);
        /* If we are not handling USBGuard configuration (e.g. the user is using
         * a third party program) we do nothing when the config changes. */
        if (usbguard_controlled) {
                g_variant_get (parameter, "s", &key);
                settings_usb = g_settings_get_uint (settings, USBGUARD);
                /* If the USBGuard configuration has been changed and doesn't match
                 * our internal state, most likely means that the user externally
                 * changed it. When this happens we set to false the control value. */
                if ((g_strcmp0 (key, APPLY_POLICY) == 0 && settings_usb == ALWAYS) ||
                    (g_strcmp0 (key, APPLY_POLICY) != 0 && settings_usb == NEVER)) {
                        g_settings_set (settings, USBGUARD_CONTROL, "b", FALSE);
                }
        }
}

static void
on_usbprotection_signal (GDBusProxy *proxy,
                         gchar      *sender_name,
                         gchar      *signal_name,
                         GVariant   *parameters,
                         gpointer    user_data)
{
        GVariant *parameter, *policy;

        if (g_strcmp0 (signal_name, "PropertyParameterChanged") != 0)
                return;

        gchar *policy_name;
        policy = g_variant_get_child_value (parameters, 0);
        g_variant_get (policy, "s", &policy_name);

        // Right now we just care about the InsertedDevicePolicy value
        if (g_strcmp0 (policy_name, INSERTED_DEVICE_POLICY) != 0)
                return;

        parameter = g_variant_get_child_value (parameters, 2);
        update_usbprotection_store (user_data, parameter);

}

static void
on_getparameter_done (GObject      *source_object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
        GVariant *parameter, *result, *params;
        gchar *key;
        guint settings_usb;
        gboolean out_of_sync = FALSE;
        GDBusConnection *bus;
        GsdUSBProtectionManager *manager = user_data;
        GSettings *settings = manager->priv->settings;
        g_autoptr(GError) error = NULL;

        result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object),
                                           res,
                                           &error);
        if (result == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Failed fetch USBGuard parameters: %s", error->message);
                return;
        }

        parameter = g_variant_get_child_value (result, 0);
        g_variant_get (parameter, "s", &key);
        settings_usb = g_settings_get_uint (settings, USBGUARD);

        if (settings_usb == ALWAYS) {
                if (g_strcmp0 (key, BLOCK) != 0) {
                        out_of_sync = TRUE;
                        params = g_variant_new ("(ss)",
                                                INSERTED_DEVICE_POLICY,
                                                BLOCK);
                }
        } else if (settings_usb == NEVER) {
                if (g_strcmp0 (key, APPLY_POLICY) != 0) {
                        out_of_sync = TRUE;
                        params = g_variant_new ("(ss)",
                                                INSERTED_DEVICE_POLICY,
                                                APPLY_POLICY);
                }
        }

        if (out_of_sync) {
                g_dbus_proxy_call (manager->priv->usbprotection,
                                   "setParameter",
                                   params,
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   manager->priv->cancellable,
                                   NULL, NULL);
        }

        /* If we are in "Never" or "When lockscreen is active" we also check
         * if the "always allow" rule is present. */
        if (settings_usb != ALWAYS) {
                params = g_variant_new ("(s)", "");
                bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL);
                g_dbus_connection_call (bus,
                                        USBGUARD_DBUS_NAME,
                                        USBGUARD_DBUS_PATH_POLICY,
                                        USBGUARD_DBUS_INTERFACE_POLICY,
                                        "listRules",
                                        params,
                                        NULL, 0, -1,
                                        manager->priv->cancellable,
                                        usbguard_listrules_cb,
                                        manager);
        }
}

static void
sync_usbprotection (GDBusProxy              *proxy,
                    GsdUSBProtectionManager *manager)
{
        GVariant *params;
        gboolean usbguard_controlled;
        GSettings *settings = manager->priv->settings;

        usbguard_controlled = g_settings_get_boolean (settings, USBGUARD_CONTROL);

        if (!usbguard_controlled)
                return;

        params = g_variant_new ("(s)", INSERTED_DEVICE_POLICY);
        g_dbus_proxy_call (proxy,
                           "getParameter",
                           params,
                           G_DBUS_CALL_FLAGS_NONE,
                           -1,
                           manager->priv->cancellable,
                           on_getparameter_done,
                           manager);
}

static void
usbprotection_proxy_ready (GObject      *source_object,
                           GAsyncResult *res,
                           gpointer      user_data)
{
        GsdUSBProtectionManager *manager = user_data;
        GDBusProxy *proxy;
        g_autoptr(GError) error = NULL;

        proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
        if (!proxy) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Failed to contact USBGuard: %s", error->message);
                return;
        }
        manager->priv->usbprotection = proxy;

        g_signal_connect (G_OBJECT (manager->priv->settings), "changed",
                          G_CALLBACK (settings_changed_callback), manager);

        sync_usbprotection (proxy, manager);

        g_signal_connect_object (source_object,
                                 "g-signal",
                                 G_CALLBACK (on_usbprotection_signal),
                                 user_data,
                                 0);
}

static gboolean
start_usbprotection_idle_cb (GsdUSBProtectionManager *manager)
{
        g_debug ("Starting usbprotection manager");

        manager->priv->settings = g_settings_new(PRIVACY_SETTINGS);
        manager->priv->cancellable = g_cancellable_new ();

        g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                  G_DBUS_PROXY_FLAGS_NONE,
                                  NULL,
                                  USBGUARD_DBUS_NAME,
                                  USBGUARD_DBUS_PATH,
                                  USBGUARD_DBUS_INTERFACE,
                                  manager->priv->cancellable,
                                  usbprotection_proxy_ready,
                                  manager);

        manager->priv->start_idle_id = 0;

        return FALSE;
}

gboolean
gsd_usbprotection_manager_start (GsdUSBProtectionManager *manager,
                                 GError                 **error)
{
        gnome_settings_profile_start (NULL);

        manager->priv->start_idle_id = g_idle_add ((GSourceFunc) start_usbprotection_idle_cb, manager);
        g_source_set_name_by_id (manager->priv->start_idle_id, "[gnome-settings-daemon] start_usbguard_idle_cb");

        gnome_settings_profile_end (NULL);
        return TRUE;
}

void
gsd_usbprotection_manager_stop (GsdUSBProtectionManager *manager)
{
        g_debug ("Stopping usbprotection manager");

        if (manager->priv->cancellable != NULL) {
                g_cancellable_cancel (manager->priv->cancellable);
                g_clear_object (&manager->priv->cancellable);
        }

        g_clear_object (&manager->priv->settings);
        g_clear_object (&manager->priv->usbprotection);
}

static GObject *
gsd_usbprotection_manager_constructor (GType                  type,
                                       guint                  n_construct_properties,
                                       GObjectConstructParam *construct_properties)
{
        GsdUSBProtectionManager *usbprotection_manager;

        usbprotection_manager = GSD_USBPROTECTION_MANAGER (G_OBJECT_CLASS (gsd_usbprotection_manager_parent_class)->constructor (type,
                                                                                                                                 n_construct_properties,
                                                                                                                                 construct_properties));

        return G_OBJECT (usbprotection_manager);
}

static void
gsd_usbprotection_manager_dispose (GObject *object)
{
        G_OBJECT_CLASS (gsd_usbprotection_manager_parent_class)->dispose (object);
}

static void
gsd_usbprotection_manager_class_init (GsdUSBProtectionManagerClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->constructor = gsd_usbprotection_manager_constructor;
        object_class->dispose = gsd_usbprotection_manager_dispose;
        object_class->finalize = gsd_usbprotection_manager_finalize;
}

static void
gsd_usbprotection_manager_init (GsdUSBProtectionManager *manager)
{
        manager->priv = gsd_usbprotection_manager_get_instance_private (manager);

}

static void
gsd_usbprotection_manager_finalize (GObject *object)
{
        GsdUSBProtectionManager *usbprotection_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_USBPROTECTION_MANAGER (object));

        usbprotection_manager = GSD_USBPROTECTION_MANAGER (object);

        g_return_if_fail (usbprotection_manager->priv != NULL);

        G_OBJECT_CLASS (gsd_usbprotection_manager_parent_class)->finalize (object);
}

GsdUSBProtectionManager *
gsd_usbprotection_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_USBPROTECTION_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_USBPROTECTION_MANAGER (manager_object);
}
