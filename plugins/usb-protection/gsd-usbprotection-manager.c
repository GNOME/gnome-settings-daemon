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

#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <libnotify/notify.h>
#include <locale.h>
#include <string.h>

#include "gnome-settings-profile.h"
#include "gsd-usbprotection-manager.h"
#include "gnome-settings-bus.h"

#define PRIVACY_SETTINGS "org.gnome.desktop.privacy"
#define USB_PROTECTION "usb-protection"
#define USB_PROTECTION_LEVEL "usb-protection-level"

#define USBGUARD_DBUS_NAME "org.usbguard"
#define USBGUARD_DBUS_PATH "/org/usbguard"
#define USBGUARD_DBUS_INTERFACE "org.usbguard"

#define USBGUARD_DBUS_PATH_POLICY USBGUARD_DBUS_PATH "/Policy"
#define USBGUARD_DBUS_INTERFACE_POLICY USBGUARD_DBUS_INTERFACE ".Policy"

#define USBGUARD_DBUS_PATH_DEVICES USBGUARD_DBUS_PATH "/Devices"
#define USBGUARD_DBUS_INTERFACE_DEVICES USBGUARD_DBUS_INTERFACE ".Devices"

#define APPLY_POLICY "apply-policy"
#define BLOCK "block"
#define REJECT "reject"

#define APPLY_DEVICE_POLICY "applyDevicePolicy"
#define LIST_DEVICES "listDevices"
#define ALLOW "allow"
#define DEVICE_PRESENCE_CHANGED "DevicePresenceChanged"
#define INSERTED_DEVICE_POLICY "InsertedDevicePolicy"
#define APPEND_RULE "appendRule"
#define ALLOW_ALL "allow id *:*"
#define WITH_INTERFACE "with-interface"

struct GsdUSBProtectionManagerPrivate
{
        guint               start_idle_id;
        GSettings          *settings;
        GDBusProxy         *usbprotection;
        GDBusProxy         *usbprotection_devices;
        GCancellable       *cancellable;
        GsdScreenSaver     *screensaver_proxy;
        gboolean            screensaver_active;
        gboolean            touchscreen_available;
        NotifyNotification *notification;
};

enum {
        NEVER,
        WITH_LOCKSCREEN,
        ALWAYS
};

enum {
        PRESENT,
        INSERT,
        UPDATE,
        REMOVE
};

enum {
        TARGET_ALLOW,
        TARGET_BLOCK,
        TARGET_REJECT
};

enum {
        DEVICE_ID,
        DEVICE_EVENT,
        TARGET,
        DEV_RULE,
        ATTRIBUTES
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

        result = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object),
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
        if (g_strcmp0 (key, USB_PROTECTION) != 0 && g_strcmp0 (key, USB_PROTECTION_LEVEL) != 0)
                return;

        usbguard_controlled = g_settings_get_boolean (settings, USB_PROTECTION);
        settings_usb_value = g_settings_get_uint (settings, USB_PROTECTION_LEVEL);
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

        usbguard_controlled = g_settings_get_boolean (settings, USB_PROTECTION);
        /* If we are not handling USBGuard configuration (e.g. the user is using
         * a third party program) we do nothing when the config changes. */
        if (usbguard_controlled) {
                g_variant_get (parameter, "s", &key);
                settings_usb = g_settings_get_uint (settings, USB_PROTECTION_LEVEL);
                /* If the USBGuard configuration has been changed and doesn't match
                 * our internal state, most likely means that the user externally
                 * changed it. When this happens we set to false the control value. */
                if ((g_strcmp0 (key, APPLY_POLICY) == 0 && settings_usb == ALWAYS) ||
                    (g_strcmp0 (key, APPLY_POLICY) != 0 && settings_usb == NEVER)) {
                        g_settings_set (settings, USB_PROTECTION, "b", FALSE);
                }
        }
}

static gboolean
is_protection_active (GsdUSBProtectionManager *manager)
{
        gboolean usbguard_controlled;
        guint protection_lvl;
        GSettings *settings = manager->priv->settings;

        usbguard_controlled = g_settings_get_boolean (settings, USB_PROTECTION);
        protection_lvl = g_settings_get_uint (settings, USB_PROTECTION_LEVEL);

        /* If we are in the option "never block" the authorization is already
         * handled with an "allow" in the rule file, so we don't need to manually
         * authorize new USB devices. */
        return usbguard_controlled && (protection_lvl != NEVER);
}

static void
on_notification_closed (NotifyNotification *n,
                        GsdUSBProtectionManager *manager)
{
        g_clear_object (&manager->priv->notification);
}

static void
show_notification (GsdUSBProtectionManager *manager,
                   const char              *summary,
                   const char              *body)
{
        /* Don't show a notice if one is already displayed */
        if (manager->priv->notification != NULL)
                return;

        manager->priv->notification = notify_notification_new (summary, body, "drive-removable-media-symbolic");
        notify_notification_set_app_name (manager->priv->notification, _("USB Protection"));
        notify_notification_set_hint (manager->priv->notification, "transient", g_variant_new_boolean (TRUE));
        notify_notification_set_timeout (manager->priv->notification, NOTIFY_EXPIRES_DEFAULT);
        notify_notification_set_urgency (manager->priv->notification, NOTIFY_URGENCY_CRITICAL);
        g_signal_connect (manager->priv->notification,
                          "closed",
                          G_CALLBACK (on_notification_closed),
                          manager);
        if (!notify_notification_show (manager->priv->notification, NULL)) {
                g_warning ("Failed to send USB protection notification");
        }
}

static void authorize_device (GDBusProxy              *proxy,
                              GsdUSBProtectionManager *manager,
                              guint                    device_id,
                              guint                    target,
                              gboolean                 permanent)
{
        GVariant *params;

        params = g_variant_new ("(uub)", device_id, target, permanent);
        g_dbus_proxy_call (manager->priv->usbprotection_devices,
                           APPLY_DEVICE_POLICY,
                           params,
                           G_DBUS_CALL_FLAGS_NONE,
                           -1,
                           manager->priv->cancellable,
                           NULL, NULL);
}

static gboolean
is_only_hid (GVariant *device)
{
        GVariant *dev;
        GVariantIter *iter = NULL;
        gchar *name, *value;
        gchar **interfaces_splitted;
        guint i;
        gboolean only_hid = TRUE;

        dev = g_variant_get_child_value (device, ATTRIBUTES);
        g_variant_get (dev, "a{ss}", &iter);
        g_variant_unref (dev);
        while (g_variant_iter_loop (iter, "{ss}", &name, &value)) {
                if (g_strcmp0 (name, WITH_INTERFACE) == 0) {
                        interfaces_splitted = g_strsplit (value, " ", -1);
                        if (interfaces_splitted) {
                                for (i = 0; i < g_strv_length (interfaces_splitted); i++)
                                        if (g_strstr_len (interfaces_splitted[i], -1, "03:") != interfaces_splitted[i])
                                                only_hid = FALSE;

                                g_strfreev (interfaces_splitted);
                        }
                }
        }
        return only_hid;
}

static gboolean
is_keyboard (GVariant *device)
{
        GVariant *dev;
        GVariantIter *iter = NULL;
        gchar *name, *value;

        dev = g_variant_get_child_value (device, ATTRIBUTES);
        g_variant_get (dev, "a{ss}", &iter);
        g_variant_unref (dev);
        while (g_variant_iter_loop (iter, "{ss}", &name, &value)) {
                if (g_strcmp0 (name, WITH_INTERFACE) == 0) {
                        return g_strrstr (value, "03:00:01") != NULL ||
                               g_strrstr (value, "03:01:01") != NULL;
                }
        }
        return FALSE;
}

static gboolean
auth_one_keyboard (GsdUSBProtectionManager *manager,
                   GVariant *device)
{
        GVariant *ret, *d_id;
        GVariantIter *iter = NULL;
        guint attr_len, dev_id, device_id;
        gchar *attr;
        gchar **attr_splitted;
        gboolean keyboard_found = FALSE;
        g_autoptr(GError) error = NULL;

        /* If this HID advertises also interfaces outside the HID class it is suspect.
         * It could be a false positive because this can be a "smart" keyboard, but at
         * this stage is better be safe.*/
        if (!is_only_hid (device))
                return FALSE;

        ret = g_dbus_proxy_call_sync (manager->priv->usbprotection_devices,
                                      LIST_DEVICES,
                                      g_variant_new ("(s)", ALLOW),
                                      G_DBUS_CALL_FLAGS_NONE,
                                      -1,
                                      manager->priv->cancellable,
                                      &error);
        if (ret == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Failed to contact USBGuard: %s", error->message);
                return FALSE;
        }
        g_variant_get (ret, "(a(us))", &iter);
        g_variant_unref (ret);
        while (g_variant_iter_loop (iter, "(us)", &dev_id, &attr)) {
                g_debug ("%s", attr);
                attr_splitted = g_strsplit (attr, WITH_INTERFACE, -1);
                if (attr_splitted) {
                        attr_len = g_strv_length (attr_splitted);
                        if (attr_len > 0)
                                if (g_strrstr (attr_splitted[attr_len - 1], "03:00:01") != NULL ||
                                    g_strrstr (attr_splitted[attr_len - 1], "03:01:01") != NULL)
                                        keyboard_found = TRUE;

                        g_strfreev (attr_splitted);
                }
        }
        if (!keyboard_found) {
                d_id = g_variant_get_child_value (device, DEVICE_ID);
                device_id = g_variant_get_uint32 (d_id);
                g_variant_unref (d_id);
                authorize_device(manager->priv->usbprotection_devices,
                                 manager,
                                 device_id,
                                 TARGET_ALLOW,
                                 FALSE);
        }
        return TRUE;
}

static void
on_screen_locked (GsdScreenSaver          *screen_saver,
                  GAsyncResult            *result,
                  GsdUSBProtectionManager *manager)
{
        gboolean is_locked;
        g_autoptr(GError) error = NULL;

        is_locked = gsd_screen_saver_call_lock_finish (screen_saver, result, &error);

        show_notification (manager,
                           _("New USB device"),
                           _("New device has been detected while the session was not locked. "
                             "If you did not plug anything, check your system for any suspicious device."));

        if (!is_locked) {
                g_warning ("Couldn't lock screen: %s", error->message);
                return;
        }
}

static void
on_device_presence_signal (GDBusProxy *proxy,
                           gchar      *sender_name,
                           gchar      *signal_name,
                           GVariant   *parameters,
                           gpointer    user_data)
{
        GVariant *ev, *ta, *d_id;
        guint device_event;
        guint target;
        guint device_id;
        guint protection_lvl;
        GsdUSBProtectionManager *manager = user_data;

        g_debug ("new device!");
        g_debug ("%s", signal_name);
        /* We do nothing if we receive a different signal from DevicePresenceChanged */
        if (g_strcmp0 (signal_name, DEVICE_PRESENCE_CHANGED) != 0)
                return;

        ev = g_variant_get_child_value (parameters, DEVICE_EVENT);
        device_event = g_variant_get_uint32 (ev);
        g_variant_unref (ev);

        g_debug ("Event: %i", device_event);
        if (device_event != INSERT)
                return;

        ta = g_variant_get_child_value (parameters, TARGET);
        target = g_variant_get_uint32 (ta);
        g_variant_unref (ta);

        g_debug ("Target: %i", target);
        /* If the device is already authorized we do nothing */
        if (target == TARGET_ALLOW)
            return;

        /* If the USB protection is disabled we do nothing */
        if (!is_protection_active (manager))
                return;

        g_debug("protection is active");
        if (manager->priv->screensaver_active) {
                /* If the session is locked we check if the inserted device is a keyboard.
                 * If this new device is the only available keyboard we authorize it.
                 * Also, if the device has touchscreen capabilities we never authorize
                 * keyboards in this stage. Because the user can very well use the
                 * on-screen virtual keyboard. */
                if (!manager->priv->touchscreen_available && is_keyboard (parameters))
                        if (auth_one_keyboard (manager, parameters)) {
                                show_notification (manager,
                                                   _("New keyboard detected"),
                                                   _("Either your keyboard has been reconnected or a new one has been plugged in. "
                                                     "If you did not do it, check your system for any suspicious device."));
                                return;
                        }

                show_notification (manager,
                                   _("Unknown USB device"),
                                   _("New device has been detected while you were away. "
                                     "Please disconnect and reconnect the device to start using it."));
                return;
        }

        protection_lvl = g_settings_get_uint (manager->priv->settings, USB_PROTECTION_LEVEL);
        if (protection_lvl == WITH_LOCKSCREEN) {
                /* We need to authorize the device. */
                d_id = g_variant_get_child_value (parameters, DEVICE_ID);
                device_id = g_variant_get_uint32 (d_id);
                g_variant_unref (d_id);
                authorize_device(proxy, manager, device_id, TARGET_ALLOW, FALSE);
        } else if (protection_lvl == ALWAYS) {
                /* We authorize the device if this is the only available keyboard.
                 * We also lock the screen to prevent an attacker to plug malicious
                 * devices if the legitimate user forgot to lock his session.
                 * As before, if there is the touchscreen we don't authorize
                 * keyboards because the user can never be locked out. */
                if (!manager->priv->touchscreen_available && is_keyboard (parameters))
                        if (auth_one_keyboard (manager, parameters))
                                gsd_screen_saver_call_lock (manager->priv->screensaver_proxy,
                                                            manager->priv->cancellable,
                                                            (GAsyncReadyCallback) on_screen_locked,
                                                            manager);
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
        gchar *policy_name;

        if (g_strcmp0 (signal_name, "PropertyParameterChanged") != 0)
                return;

        policy = g_variant_get_child_value (parameters, 0);
        g_variant_get (policy, "s", &policy_name);
        g_variant_unref (policy);

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
        g_variant_unref (parameter);
        g_variant_unref (result);
        settings_usb = g_settings_get_uint (settings, USB_PROTECTION_LEVEL);

        if (settings_usb == ALWAYS || settings_usb == WITH_LOCKSCREEN) {
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
                g_variant_unref (params);
        }

        /* If we are in "Never" or "When lockscreen is active" we also check
         * if the "always allow" rule is present. */
        if (settings_usb != ALWAYS) {
                bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL);
                g_dbus_connection_call (bus,
                                        USBGUARD_DBUS_NAME,
                                        USBGUARD_DBUS_PATH_POLICY,
                                        USBGUARD_DBUS_INTERFACE_POLICY,
                                        "listRules",
                                        g_variant_new ("(s)", ""),
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

        usbguard_controlled = g_settings_get_boolean (settings, USB_PROTECTION);

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
on_usbprotection_owner_changed_cb (GObject    *object,
                                   GParamSpec *pspec,
                                   gpointer    user_data)
{
        GsdUSBProtectionManager *manager = user_data;
        //GDBusProxy *proxy = manager->priv->usbprotection;
        GDBusProxy *proxy = G_DBUS_PROXY(object);
        char *name_owner;

        name_owner = g_dbus_proxy_get_name_owner (proxy);
        g_debug("hey hey, the owner changed!");
        if (name_owner) {
                g_debug("%s", name_owner);
                //TODO: if USBGuard is installed while g-s-d is running we notice it here,
                // but the proxy is unusable.
                // GDBus.Error:org.freedesktop.DBus.Error.NoServer: USBGuard DBus
                // service is not connected to the daemon
                sync_usbprotection (proxy, manager);
                g_free(name_owner);
        }
}

static void
handle_screensaver_active (GsdUSBProtectionManager *manager,
                           GVariant                *parameters)
{
        gboolean active;

        g_variant_get (parameters, "(b)", &active);
        g_debug ("Received screensaver ActiveChanged signal: %d (old: %d)", active, manager->priv->screensaver_active);
        if (manager->priv->screensaver_active != active) {
                manager->priv->screensaver_active = active;

                /* probably we don't need to do anything more here */
                if (active)
                        g_debug("active");
        }
}

static void
initialize_touchscreen_search (GsdUSBProtectionManager *manager)
{
        GdkDisplay *display;
        GdkSeat *seat;
        GList *devices;

        /* If we don't initialize gtk we will get NULL from
         * gdk_display_get_default () */
        gtk_init(NULL, NULL);
        display = gdk_display_get_default ();
        seat = gdk_display_get_default_seat (display);
        devices = NULL;

        /* Adding a touchscreen at runtime is even a thing??
         * Probably we don't need to worry about that. */
        //g_signal_connect_object (seat, "device-added", G_CALLBACK (on_device_added), backend, 0);
        //g_signal_connect_object (seat, "device-removed", G_CALLBACK (on_device_removed), backend, 0);

        devices = g_list_append (devices, gdk_seat_get_pointer (seat));
        devices = g_list_append (devices, gdk_seat_get_keyboard (seat));
        /* Probably GDK_SEAT_CAPABILITY_TOUCH should be enough here. We need
         * someone with a touch enabled device to test it and report back. */
        devices = g_list_concat (devices, gdk_seat_get_slaves (seat, GDK_SEAT_CAPABILITY_ALL));

        for (; devices != NULL; devices = devices->next)
                if (gdk_device_get_source (devices->data) == GDK_SOURCE_TOUCHSCREEN)
                        manager->priv->touchscreen_available = TRUE;
}

static void
screensaver_signal_cb (GDBusProxy *proxy,
                       const gchar *sender_name,
                       const gchar *signal_name,
                       GVariant *parameters,
                       gpointer user_data)
{
        if (g_strcmp0 (signal_name, "ActiveChanged") == 0)
                handle_screensaver_active (GSD_USBPROTECTION_MANAGER (user_data), parameters);
}

static void
usbprotection_devices_proxy_ready (GObject      *source_object,
                                   GAsyncResult *res,
                                   gpointer      user_data)
{
        GsdUSBProtectionManager *manager = user_data;
        GDBusProxy *proxy;
        g_autoptr(GError) error = NULL;

        g_debug("devices ready");
        proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
        if (!proxy) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Failed to contact USBGuard: %s", error->message);
                return;
        }
        manager->priv->usbprotection_devices = proxy;

        //TODO: investigate what to do with the name-owner

        g_signal_connect_object (source_object,
                                 "g-signal",
                                 G_CALLBACK (on_device_presence_signal),
                                 user_data,
                                 0);
}

static void
usbprotection_proxy_ready (GObject      *source_object,
                           GAsyncResult *res,
                           gpointer      user_data)
{
        GsdUSBProtectionManager *manager = user_data;
        GDBusProxy *proxy;
        const char *name_owner;
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

        manager->priv->screensaver_proxy = gnome_settings_bus_get_screen_saver_proxy ();

        g_signal_connect (manager->priv->screensaver_proxy, "g-signal",
                          G_CALLBACK (screensaver_signal_cb), manager);

        name_owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (proxy));

        if (name_owner == NULL) {
                g_debug("Probably USBGuard is not currently installed.");
        } else {
                sync_usbprotection (proxy, manager);
        }

        initialize_touchscreen_search (manager);

        g_signal_connect_object (source_object,
                                 "notify::g-name-owner",
                                 G_CALLBACK (on_usbprotection_owner_changed_cb),
                                 user_data,
                                 0);

        g_signal_connect_object (source_object,
                                 "g-signal",
                                 G_CALLBACK (on_usbprotection_signal),
                                 user_data,
                                 0);

        g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                  G_DBUS_PROXY_FLAGS_NONE,
                                  NULL,
                                  USBGUARD_DBUS_NAME,
                                  USBGUARD_DBUS_PATH_DEVICES,
                                  USBGUARD_DBUS_INTERFACE_DEVICES,
                                  manager->priv->cancellable,
                                  usbprotection_devices_proxy_ready,
                                  manager);
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

        notify_init ("gnome-settings-daemon");

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

        if (manager->priv->notification != NULL) {
                g_signal_handlers_disconnect_by_func (manager->priv->notification,
                                                      G_CALLBACK (on_notification_closed),
                                                      manager);
                g_clear_object (&manager->priv->notification);
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
