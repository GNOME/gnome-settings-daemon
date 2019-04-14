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
#include "gsd-usb-protection-manager.h"
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
#define LIST_RULES "listRules"
#define ALLOW "allow"
#define DEVICE_PRESENCE_CHANGED "DevicePresenceChanged"
#define INSERTED_DEVICE_POLICY "InsertedDevicePolicy"
#define APPEND_RULE "appendRule"
#define ALLOW_ALL "allow id *:*"
#define WITH_INTERFACE "with-interface"

struct _GsdUsbProtectionManager
{
        GObject             parent;
        guint               start_idle_id;
        GSettings          *settings;
        GDBusProxy         *usb_protection;
        GDBusProxy         *usb_protection_devices;
        GDBusProxy         *usb_protection_policy;
        GCancellable       *cancellable;
        GsdScreenSaver     *screensaver_proxy;
        gboolean            screensaver_active;
        gboolean            touchscreen_available;
        NotifyNotification *notification;
};

typedef enum {
        LEVEL_WITH_LOCKSCREEN = 1,
        LEVEL_ALWAYS = 2
} UsbProtectionLevel;

typedef enum {
        EVENT_PRESENT,
        EVENT_INSERT,
        EVENT_UPDATE,
        EVENT_REMOVE
} UsbGuardEvent;

typedef enum {
        TARGET_ALLOW,
        TARGET_BLOCK,
        TARGET_REJECT
} UsbGuardTarget;

typedef enum {
        POLICY_DEVICE_ID,
        POLICY_DEVICE_EVENT,
        POLICY_TARGET,
        POLICY_DEV_RULE,
        POLICY_ATTRIBUTES
} UsbGuardPolicyChanged;

static void gsd_usb_protection_manager_finalize (GObject *object);

G_DEFINE_TYPE (GsdUsbProtectionManager, gsd_usb_protection_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static void
dbus_call_log_error (GObject      *source_object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
        g_autoptr(GVariant) result;
        g_autoptr(GError) error = NULL;
        const gchar *msg = user_data;

        result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object),
                                           res,
                                           &error);
        if (result == NULL)
                g_warning ("%s: %s", msg, error->message);
}

static void
add_usbguard_allow_rule (GsdUsbProtectionManager *manager)
{
        /* This prepends an "allow all" rule.
         * It has a double purpose. If the protection is disabled it is used
         * to ensure that new devices gets automatically authorized.
         * On top of that it is also used as an anti lockout precaution.
         * If something unexpected happens and the user is unable to authorize
         * his main keyboard he can reboot the system and, thanks to
         * this "allow all" rule, every already plugged in devices at boot time
         * will be automatically authorized. */

        GVariant *params;
        params = g_variant_new ("(su)", ALLOW_ALL, 0);
        if (manager->usb_protection_policy != NULL)
                g_dbus_proxy_call (manager->usb_protection_policy,
                                   APPEND_RULE,
                                   params,
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   manager->cancellable,
                                   dbus_call_log_error,
                                   "Error appending USBGuard rule");
}

static gboolean
is_usbguard_allow_rule_present (GVariant *rules)
{
        GVariantIter *iter = NULL;
        g_autofree gchar *value = NULL;
        guint number = 0;

        g_variant_get (rules, "a(us)", &iter);
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

        if (!result) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Failed fetch USBGuard rules list: %s", error->message);
                return;
        }

        rules = g_variant_get_child_value (result, 0);
        g_variant_unref (result);
        if (!is_usbguard_allow_rule_present (rules))
                add_usbguard_allow_rule (user_data);

}

static void
usbguard_ensure_allow_rule (GsdUsbProtectionManager *manager)
{
        g_autoptr(GVariant) params = NULL;
        if (manager->usb_protection_policy != NULL) {
                /* listRules parameter is a query for matching rules.
                 * With an empty string we get all the available rules. */
                params = g_variant_new ("(s)", "");
                g_dbus_proxy_call (manager->usb_protection_policy,
                                   LIST_RULES,
                                   params,
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   manager->cancellable,
                                   usbguard_listrules_cb,
                                   manager);
        }
}

static void
settings_changed_callback (GSettings               *settings,
                           const char              *key,
                           GsdUsbProtectionManager *manager)
{
        gchar *value_usbguard;
        UsbProtectionLevel protection_lvl;
        gboolean usbguard_controlled;
        GVariant *params;

        /* We react only if one of the two USB related properties has been changed */
        if (g_strcmp0 (key, USB_PROTECTION) != 0 && g_strcmp0 (key, USB_PROTECTION_LEVEL) != 0)
                return;

        usbguard_controlled = g_settings_get_boolean (settings, USB_PROTECTION);
        protection_lvl = g_settings_get_uint (settings, USB_PROTECTION_LEVEL);
        g_debug ("USBGuard control is currently %i with a protection level of %i",
                 usbguard_controlled, protection_lvl);

        /* If previously we were controlling USBGuard and now we are not,
         * we leave the USBGuard configuration in a clean state. I.e. we set
         * "InsertedDevicePolicy" to "apply-policy" and we ensure that
         * there is an always allow rule. In this way even if USBGuard daemon
         * is running every USB devices will be automatically authorized. */
        if (g_strcmp0 (key, USB_PROTECTION) == 0 && !usbguard_controlled) {
                g_debug ("let's clean usbguard config state");
                params = g_variant_new ("(ss)",
                                        INSERTED_DEVICE_POLICY,
                                        APPLY_POLICY);

                if (manager->usb_protection != NULL) {
                        g_dbus_proxy_call (manager->usb_protection,
                                           "setParameter",
                                           params,
                                           G_DBUS_CALL_FLAGS_NONE,
                                           -1,
                                           manager->cancellable,
                                           dbus_call_log_error,
                                           "Error calling USBGuard DBus");
                }

                usbguard_ensure_allow_rule (manager);
        }

        /* Only if we are entitled to handle USBGuard */
        if (usbguard_controlled && manager->usb_protection != NULL) {
                value_usbguard = (protection_lvl == LEVEL_ALWAYS) ? BLOCK : APPLY_POLICY;
                params = g_variant_new ("(ss)",
                                        INSERTED_DEVICE_POLICY,
                                        value_usbguard);

                g_dbus_proxy_call (manager->usb_protection,
                                   "setParameter",
                                   params,
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   manager->cancellable,
                                   dbus_call_log_error,
                                   "Error calling USBGuard DBus");

                /* If we are in "When lockscreen is active" we also check if the
                 * always allow rule is present. */
                if (protection_lvl == LEVEL_WITH_LOCKSCREEN)
                        usbguard_ensure_allow_rule (manager);
        }
}

static void update_usb_protection_store (GsdUsbProtectionManager *manager,
                                         GVariant                *parameter)
{
        gchar *key;
        gboolean usbguard_controlled;
        UsbProtectionLevel protection_lvl;
        GSettings *settings = manager->settings;

        usbguard_controlled = g_settings_get_boolean (settings, USB_PROTECTION);
        /* If we are not handling USBGuard configuration (e.g. the user is using
         * a third party program) we do nothing when the config changes. */
        if (usbguard_controlled) {
                g_variant_get (parameter, "s", &key);
                protection_lvl = g_settings_get_uint (settings, USB_PROTECTION_LEVEL);
                /* If the USBGuard configuration has been changed and doesn't match
                 * our internal state, most likely means that the user externally
                 * changed it. When this happens we set to false the control value. */
                if ((g_strcmp0 (key, APPLY_POLICY) == 0 && protection_lvl == LEVEL_ALWAYS))
                        g_settings_set (settings, USB_PROTECTION, "b", FALSE);
                
                g_free (key);
        }
}

static gboolean
is_protection_active (GsdUsbProtectionManager *manager)
{
        GSettings *settings = manager->settings;

        return g_settings_get_boolean (settings, USB_PROTECTION);
}

static void
on_notification_closed (NotifyNotification *n,
                        GsdUsbProtectionManager *manager)
{
        g_clear_object (&manager->notification);
}

static void
show_notification (GsdUsbProtectionManager *manager,
                   const char              *summary,
                   const char              *body)
{
        /* Don't show a notice if one is already displayed */
        if (manager->notification != NULL)
                return;

        manager->notification = notify_notification_new (summary, body, "drive-removable-media-symbolic");
        notify_notification_set_app_name (manager->notification, _("USB Protection"));
        notify_notification_set_hint (manager->notification, "transient", g_variant_new_boolean (TRUE));
        notify_notification_set_timeout (manager->notification, NOTIFY_EXPIRES_DEFAULT);
        notify_notification_set_urgency (manager->notification, NOTIFY_URGENCY_CRITICAL);
        g_signal_connect (manager->notification,
                          "closed",
                          G_CALLBACK (on_notification_closed),
                          manager);
        if (!notify_notification_show (manager->notification, NULL)) {
                g_warning ("Failed to send USB protection notification");
        }
}

static void authorize_device (GDBusProxy              *proxy,
                              GsdUsbProtectionManager *manager,
                              guint                    device_id,
                              guint                    target,
                              gboolean                 permanent)
{
        GVariant *params;

        params = g_variant_new ("(uub)", device_id, target, permanent);
        if (manager->usb_protection_devices != NULL)
                g_dbus_proxy_call (manager->usb_protection_devices,
                                   APPLY_DEVICE_POLICY,
                                   params,
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   manager->cancellable,
                                   dbus_call_log_error,
                                   "Error calling USBGuard DBus");
}

static gboolean
is_only_hid (GVariant *device)
{
        GVariantIter *iter = NULL;
        g_autofree gchar *name = NULL;
        g_autofree gchar *value = NULL;
        gchar **interfaces_splitted;
        guint i;
        gboolean only_hid = TRUE;

        g_variant_get_child (device, POLICY_ATTRIBUTES, "a{ss}", &iter);
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
        GVariantIter *iter = NULL;
        g_autofree gchar *name = NULL;
        g_autofree gchar *value = NULL;

        g_variant_get_child (device, POLICY_ATTRIBUTES, "a{ss}", &iter);
        while (g_variant_iter_loop (iter, "{ss}", &name, &value)) {
                if (g_strcmp0 (name, WITH_INTERFACE) == 0) {
                        return g_strrstr (value, "03:00:01") != NULL ||
                               g_strrstr (value, "03:01:01") != NULL;
                }
        }
        return FALSE;
}

static gboolean
auth_one_keyboard (GsdUsbProtectionManager *manager,
                   GVariant *device)
{
        GVariant *ret;
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

        if (manager->usb_protection_devices == NULL)
                return FALSE;

        ret = g_dbus_proxy_call_sync (manager->usb_protection_devices,
                                      LIST_DEVICES,
                                      g_variant_new ("(s)", ALLOW),
                                      G_DBUS_CALL_FLAGS_NONE,
                                      -1,
                                      manager->cancellable,
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
                g_variant_get_child (device, POLICY_DEVICE_ID, "u", &device_id);
                authorize_device(manager->usb_protection_devices,
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
                  GsdUsbProtectionManager *manager)
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
        UsbGuardEvent device_event;
        UsbGuardTarget target;
        guint device_id;
        UsbProtectionLevel protection_lvl;
        GsdUsbProtectionManager *manager = user_data;

        /* We act only if we receive a signal from DevicePresenceChanged */
        if (g_strcmp0 (signal_name, DEVICE_PRESENCE_CHANGED) != 0)
                return;

        g_variant_get_child (parameters, POLICY_DEVICE_EVENT, "u", &device_event);

        /* If this is not an insert event we do nothing */
        if (device_event != EVENT_INSERT)
                return;

        g_variant_get_child (parameters, POLICY_TARGET, "u", &target);

        /* If the device is already authorized we do nothing */
        if (target == TARGET_ALLOW)
            return;

        /* If the USB protection is disabled we do nothing */
        if (!is_protection_active (manager))
                return;

        protection_lvl = g_settings_get_uint (manager->settings, USB_PROTECTION_LEVEL);

        if (manager->screensaver_active) {
                /* If the session is locked we check if the inserted device is a keyboard.
                 * If this new device is the only available keyboard we authorize it.
                 * Also, if the device has touchscreen capabilities we never authorize
                 * keyboards in this stage because the user can very well use the
                 * on-screen virtual keyboard. */
                if (!manager->touchscreen_available && is_keyboard (parameters))
                        if (auth_one_keyboard (manager, parameters)) {
                                show_notification (manager,
                                                   _("New keyboard detected"),
                                                   _("Either your keyboard has been reconnected or a new one has been plugged in. "
                                                     "If you did not do it, check your system for any suspicious device."));
                                return;
                        }
                if (protection_lvl == LEVEL_WITH_LOCKSCREEN)
                        show_notification (manager,
                                           _("Unknown USB device"),
                                           _("New device has been detected while you were away. "
                                             "Please disconnect and reconnect the device to start using it."));
                else
                        show_notification (manager,
                                           _("Unknown USB device"),
                                           _("New device has been detected while you were away. "
                                             "It has blocked because the USB protection is active."));
                return;
        }

        if (protection_lvl == LEVEL_WITH_LOCKSCREEN) {
                /* We need to authorize the device. */
                g_variant_get_child (parameters, POLICY_DEVICE_ID, "u", &device_id);
                authorize_device(proxy, manager, device_id, TARGET_ALLOW, FALSE);
        } else if (protection_lvl == LEVEL_ALWAYS) {
                /* We authorize the device if this is the only available keyboard.
                 * We also lock the screen to prevent an attacker to plug malicious
                 * devices if the legitimate user forgot to lock his session.
                 * As before, if there is the touchscreen we don't authorize
                 * keyboards because the user can never be locked out. */
                if (!manager->touchscreen_available && is_keyboard (parameters))
                        if (auth_one_keyboard (manager, parameters)) {
                                gsd_screen_saver_call_lock (manager->screensaver_proxy,
                                                            manager->cancellable,
                                                            (GAsyncReadyCallback) on_screen_locked,
                                                            manager);
                                return;
                        }
                show_notification (manager,
                                   _("Unknown USB device"),
                                   _("The new inserted device has blocked because the USB protection is active."));
        }
}

static void
on_usb_protection_signal (GDBusProxy *proxy,
                          gchar      *sender_name,
                          gchar      *signal_name,
                          GVariant   *parameters,
                          gpointer    user_data)
{
        g_autoptr(GVariant) parameter = NULL;
        g_autofree gchar *policy_name = NULL;

        if (g_strcmp0 (signal_name, "PropertyParameterChanged") != 0)
                return;

        g_variant_get_child (parameters, 0, "s", &policy_name);

        /* Right now we just care about the InsertedDevicePolicy value */
        if (g_strcmp0 (policy_name, INSERTED_DEVICE_POLICY) != 0)
                return;

        parameter = g_variant_get_child_value (parameters, 2);
        update_usb_protection_store (user_data, parameter);

}

static void
on_getparameter_done (GObject      *source_object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
        GVariant *result, *params;
        g_autofree gchar *key = NULL;
        UsbProtectionLevel protection_lvl;
        gboolean out_of_sync = FALSE;
        GsdUsbProtectionManager *manager;
        GSettings *settings;
        g_autoptr(GError) error = NULL;

        result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object),
                                           res,
                                           &error);
        if (result == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Failed fetch USBGuard parameters: %s", error->message);
                return;
        }

        manager = GSD_USB_PROTECTION_MANAGER (user_data);
        settings = manager->settings;

        g_variant_get_child (result, 0, "s", &key);
        g_variant_unref (result);
        protection_lvl = g_settings_get_uint (settings, USB_PROTECTION_LEVEL);

        if (protection_lvl == LEVEL_ALWAYS || protection_lvl == LEVEL_WITH_LOCKSCREEN) {
                if (g_strcmp0 (key, BLOCK) != 0) {
                        out_of_sync = TRUE;
                        params = g_variant_new ("(ss)",
                                                INSERTED_DEVICE_POLICY,
                                                BLOCK);
                }
        }

        if (out_of_sync && manager->usb_protection != NULL) {
                g_dbus_proxy_call (manager->usb_protection,
                                   "setParameter",
                                   params,
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   manager->cancellable,
                                   dbus_call_log_error,
                                   "Error calling USBGuard DBus");
        }

        /* If we are in "When lockscreen is active" we also check
         * if the "always allow" rule is present. */
        if (protection_lvl == LEVEL_WITH_LOCKSCREEN)
                usbguard_ensure_allow_rule (manager);
}

static void
sync_usb_protection (GDBusProxy              *proxy,
                     GsdUsbProtectionManager *manager)
{
        GVariant *params;
        gboolean usbguard_controlled;
        GSettings *settings = manager->settings;

        usbguard_controlled = g_settings_get_boolean (settings, USB_PROTECTION);

        if (!usbguard_controlled || proxy == NULL)
                return;

        params = g_variant_new ("(s)", INSERTED_DEVICE_POLICY);
        g_dbus_proxy_call (proxy,
                           "getParameter",
                           params,
                           G_DBUS_CALL_FLAGS_NONE,
                           -1,
                           manager->cancellable,
                           on_getparameter_done,
                           manager);
}

static void
on_usb_protection_owner_changed_cb (GObject    *object,
                                    GParamSpec *pspec,
                                    gpointer    user_data)
{
        GsdUsbProtectionManager *manager = user_data;
        GDBusProxy *proxy = G_DBUS_PROXY(object);
        g_autofree gchar *name_owner = NULL;

        name_owner = g_dbus_proxy_get_name_owner (proxy);
        if (name_owner)
                sync_usb_protection (proxy, manager);
}

static void
handle_screensaver_active (GsdUsbProtectionManager *manager,
                           GVariant                *parameters)
{
        gboolean active;

        g_variant_get (parameters, "(b)", &active);
        g_debug ("Received screensaver ActiveChanged signal: %d (old: %d)", active, manager->screensaver_active);
        if (manager->screensaver_active != active)
                manager->screensaver_active = active;
}

static void
initialize_touchscreen_search (GsdUsbProtectionManager *manager)
{
        GdkDisplay *display;
        GdkSeat *seat;
        GList *devices;

        display = gdk_display_get_default ();
        seat = gdk_display_get_default_seat (display);
        devices = NULL;

        /* We don't add signals to device-added and device-removed because it's
         * highly unlikely to add touchscreen capabilities at runtime. */

        devices = g_list_append (devices, gdk_seat_get_pointer (seat));
        devices = g_list_append (devices, gdk_seat_get_keyboard (seat));
        /* Probably GDK_SEAT_CAPABILITY_TOUCH should be enough here. We need
         * someone with a touch enabled device to test it and report back. */
        devices = g_list_concat (devices, gdk_seat_get_slaves (seat, GDK_SEAT_CAPABILITY_ALL));

        for (; devices != NULL; devices = devices->next)
                if (gdk_device_get_source (devices->data) == GDK_SOURCE_TOUCHSCREEN)
                        manager->touchscreen_available = TRUE;
}

static void
screensaver_signal_cb (GDBusProxy *proxy,
                       const gchar *sender_name,
                       const gchar *signal_name,
                       GVariant *parameters,
                       gpointer user_data)
{
        if (g_strcmp0 (signal_name, "ActiveChanged") == 0)
                handle_screensaver_active (GSD_USB_PROTECTION_MANAGER (user_data), parameters);
}

static void
usb_protection_policy_proxy_ready (GObject      *source_object,
                                   GAsyncResult *res,
                                   gpointer      user_data)
{
        GsdUsbProtectionManager *manager;
        GDBusProxy *proxy;
        g_autoptr(GError) error = NULL;

        proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
        if (!proxy) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Failed to contact USBGuard: %s", error->message);
                return;
        }
        manager = GSD_USB_PROTECTION_MANAGER (user_data);
        manager->usb_protection_policy = proxy;
}

static void
usb_protection_devices_proxy_ready (GObject      *source_object,
                                    GAsyncResult *res,
                                    gpointer      user_data)
{
        GsdUsbProtectionManager *manager;
        GDBusProxy *proxy;
        g_autoptr(GError) error = NULL;

        proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
        if (!proxy) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Failed to contact USBGuard: %s", error->message);
                return;
        }
        manager = GSD_USB_PROTECTION_MANAGER (user_data);
        manager->usb_protection_devices = proxy;

        /* We don't care about already plugged in devices because they'll be
         * already autorized by the "allow all" rule in USBGuard. */
        g_signal_connect_object (source_object,
                                 "g-signal",
                                 G_CALLBACK (on_device_presence_signal),
                                 user_data,
                                 0);
}

static void
get_current_screen_saver_status (GsdUsbProtectionManager *manager)
{
        g_autoptr(GVariant) ret = NULL;
        g_autoptr(GError) error = NULL;

        ret = g_dbus_proxy_call_sync (G_DBUS_PROXY (manager->screensaver_proxy),
                                      "GetActive",
                                      NULL,
                                      G_DBUS_CALL_FLAGS_NONE,
                                      -1,
                                      manager->cancellable,
                                      &error);
        if (ret == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Failed to get screen saver status: %s", error->message);
                return;
        }
        handle_screensaver_active (manager, ret);
}

static void
usb_protection_proxy_ready (GObject      *source_object,
                            GAsyncResult *res,
                            gpointer      user_data)
{
        GsdUsbProtectionManager *manager;
        GDBusProxy *proxy;
        g_autofree gchar *name_owner = NULL;
        g_autoptr(GError) error = NULL;

        proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
        if (!proxy) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Failed to contact USBGuard: %s", error->message);
                return;
        }
        manager = GSD_USB_PROTECTION_MANAGER (user_data);
        manager->usb_protection = proxy;

        g_signal_connect (G_OBJECT (manager->settings), "changed",
                          G_CALLBACK (settings_changed_callback), manager);

        manager->screensaver_proxy = gnome_settings_bus_get_screen_saver_proxy ();

        get_current_screen_saver_status (manager);

        g_signal_connect (manager->screensaver_proxy, "g-signal",
                          G_CALLBACK (screensaver_signal_cb), manager);

        name_owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (proxy));

        if (name_owner == NULL)
                g_debug("Probably USBGuard is not currently installed.");
        else
                sync_usb_protection (proxy, manager);

        initialize_touchscreen_search (manager);

        g_signal_connect_object (source_object,
                                 "notify::g-name-owner",
                                 G_CALLBACK (on_usb_protection_owner_changed_cb),
                                 user_data,
                                 0);

        g_signal_connect_object (source_object,
                                 "g-signal",
                                 G_CALLBACK (on_usb_protection_signal),
                                 user_data,
                                 0);

        g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                  G_DBUS_PROXY_FLAGS_NONE,
                                  NULL,
                                  USBGUARD_DBUS_NAME,
                                  USBGUARD_DBUS_PATH_DEVICES,
                                  USBGUARD_DBUS_INTERFACE_DEVICES,
                                  manager->cancellable,
                                  usb_protection_devices_proxy_ready,
                                  manager);

        g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                  G_DBUS_PROXY_FLAGS_NONE,
                                  NULL,
                                  USBGUARD_DBUS_NAME,
                                  USBGUARD_DBUS_PATH_POLICY,
                                  USBGUARD_DBUS_INTERFACE_POLICY,
                                  manager->cancellable,
                                  usb_protection_policy_proxy_ready,
                                  manager);
}

static gboolean
start_usb_protection_idle_cb (GsdUsbProtectionManager *manager)
{
        g_debug ("Starting USB protection manager");

        manager->settings = g_settings_new (PRIVACY_SETTINGS);
        manager->cancellable = g_cancellable_new ();

        g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                  G_DBUS_PROXY_FLAGS_NONE,
                                  NULL,
                                  USBGUARD_DBUS_NAME,
                                  USBGUARD_DBUS_PATH,
                                  USBGUARD_DBUS_INTERFACE,
                                  manager->cancellable,
                                  usb_protection_proxy_ready,
                                  manager);

        notify_init ("gnome-settings-daemon");

        manager->start_idle_id = 0;

        return FALSE;
}

gboolean
gsd_usb_protection_manager_start (GsdUsbProtectionManager *manager,
                                  GError                 **error)
{
        gnome_settings_profile_start (NULL);

        manager->start_idle_id = g_idle_add ((GSourceFunc) start_usb_protection_idle_cb, manager);
        g_source_set_name_by_id (manager->start_idle_id, "[gnome-settings-daemon] start_usbguard_idle_cb");

        gnome_settings_profile_end (NULL);
        return TRUE;
}

void
gsd_usb_protection_manager_stop (GsdUsbProtectionManager *manager)
{
        g_debug ("Stopping USB protection manager");

        if (manager->cancellable != NULL) {
                g_cancellable_cancel (manager->cancellable);
                g_clear_object (&manager->cancellable);
        }

        if (manager->notification != NULL) {
                g_signal_handlers_disconnect_by_func (manager->notification,
                                                      G_CALLBACK (on_notification_closed),
                                                      manager);
                g_clear_object (&manager->notification);
        }

        if (manager->start_idle_id != 0) {
                g_source_remove (manager->start_idle_id);
                manager->start_idle_id = 0;
        }

        g_clear_object (&manager->settings);

        if (manager->usb_protection != NULL)
                g_clear_object (&manager->usb_protection);

        if (manager->usb_protection_devices != NULL)
                g_clear_object (&manager->usb_protection_devices);

        if (manager->usb_protection_policy != NULL)
                g_clear_object (&manager->usb_protection_policy);

        if (manager->screensaver_proxy != NULL)
                g_clear_object (&manager->screensaver_proxy);
}

static void
gsd_usb_protection_manager_class_init (GsdUsbProtectionManagerClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_usb_protection_manager_finalize;
}

static void
gsd_usb_protection_manager_init (GsdUsbProtectionManager *manager)
{
}

static void
gsd_usb_protection_manager_finalize (GObject *object)
{
        GsdUsbProtectionManager *usb_protection_manager;

        g_return_if_fail (GSD_IS_USB_PROTECTION_MANAGER (object));

        usb_protection_manager = GSD_USB_PROTECTION_MANAGER (object);

        g_return_if_fail (usb_protection_manager != NULL);

        gsd_usb_protection_manager_stop (usb_protection_manager);

        G_OBJECT_CLASS (gsd_usb_protection_manager_parent_class)->finalize (object);
}

GsdUsbProtectionManager *
gsd_usb_protection_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_USB_PROTECTION_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_USB_PROTECTION_MANAGER (manager_object);
}
