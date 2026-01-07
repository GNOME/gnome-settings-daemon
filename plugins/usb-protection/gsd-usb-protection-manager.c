/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2022 Tobias Mueller <tobiasmue@gnome.org>
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

#include <gdesktop-enums.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <libnotify/notify.h>

#include "gsd-usb-protection-manager.h"

#include "gnome-settings-bus.h"
#include "gnome-settings-profile.h"
#include "gsd-usb-protection-braille-devices.h"

#define PRIVACY_SETTINGS "org.gnome.desktop.privacy"
#define USB_PROTECTION "usb-protection"
#define USB_PROTECTION_LEVEL "usb-protection-level"

#define DBUS_VERSION "1"

#define USBGUARD_DBUS_NAME "org.usbguard" DBUS_VERSION
#define USBGUARD_DBUS_PATH "/org/usbguard" DBUS_VERSION
#define USBGUARD_DBUS_INTERFACE "org.usbguard"
#define USBGUARD_DBUS_INTERFACE_VERSIONED USBGUARD_DBUS_INTERFACE DBUS_VERSION

#define USBGUARD_DBUS_PATH_POLICY USBGUARD_DBUS_PATH "/Policy"
#define USBGUARD_DBUS_INTERFACE_POLICY USBGUARD_DBUS_INTERFACE ".Policy" DBUS_VERSION

#define USBGUARD_DBUS_PATH_DEVICES USBGUARD_DBUS_PATH "/Devices"
#define USBGUARD_DBUS_INTERFACE_DEVICES USBGUARD_DBUS_INTERFACE ".Devices" DBUS_VERSION

#define USBGUARD_LAST_RULE_ID G_MAXUINT32 - 2

#define APPLY_POLICY "apply-policy"
#define BLOCK "block"
#define REJECT "reject"

#define APPLY_DEVICE_POLICY "applyDevicePolicy"
#define LIST_DEVICES "listDevices"
#define LIST_RULES "listRules"
#define ALLOW "allow"
#define INSERTED_DEVICE_POLICY "InsertedDevicePolicy"
#define APPEND_RULE "appendRule"
#define ALLOW_ALL "allow id *:* label \"GNOME_SETTINGS_DAEMON_RULE\""
#define WITH_CONNECT_TYPE "with-connect-type"
#define WITH_INTERFACE "with-interface"
#define NAME "name"

struct _GsdUsbProtectionManager
{
        GsdApplication      parent;
        guint               start_idle_id;
        GDBusNodeInfo      *introspection_data;
        GSettings          *settings;
        guint               name_id;
        GDBusConnection    *connection;
        gboolean            available;
        GDBusProxy         *usb_protection;
        GDBusProxy         *usb_protection_devices;
        GDBusProxy         *usb_protection_policy;
        GCancellable       *cancellable;
        GsdScreenSaver     *screensaver_proxy;
        GsdSessionManager  *session_proxy;
        gboolean            screensaver_active;
        gboolean            session_locked;
        NotifyNotification *notification;
        GHashTable         *braille_devices;
};


typedef enum {
        TARGET_ALLOW,
        TARGET_BLOCK,
        TARGET_REJECT
} UsbGuardTarget;


/** Elements of the DevicePolicyApplied signal */
typedef enum {
        POLICY_APPLIED_DEVICE_ID,
        POLICY_APPLIED_TARGET,
        POLICY_APPLIED_DEV_RULE,
        POLICY_APPLIED_RULE_ID,
        POLICY_APPLIED_ATTRIBUTES
} UsbGuardPolicyApplied;

G_DEFINE_TYPE (GsdUsbProtectionManager, gsd_usb_protection_manager, GSD_TYPE_APPLICATION)

#define GSD_DBUS_NAME "org.gnome.SettingsDaemon"
#define GSD_DBUS_PATH "/org/gnome/SettingsDaemon"
#define GSD_DBUS_BASE_INTERFACE "org.gnome.SettingsDaemon"

#define GSD_USB_PROTECTION_DBUS_NAME GSD_DBUS_NAME ".UsbProtection"
#define GSD_USB_PROTECTION_DBUS_PATH GSD_DBUS_PATH "/UsbProtection"

static const gchar introspection_xml[] =
"<node>"
"  <interface name='org.gnome.SettingsDaemon.UsbProtection'>"
"    <property name='Available' type='b' access='read'/>"
"  </interface>"
"</node>";

static void sync_usb_protection (GsdUsbProtectionManager *manager);

static void
dbus_call_log_error (GObject      *source_object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
        g_autoptr(GVariant) result = NULL;
        g_autoptr(GError) error = NULL;
        const gchar *msg = user_data;

        result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object),
                                           res,
                                           &error);
        if (result == NULL &&
            !g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN))
                g_warning ("%s: %s", msg, error->message);
}


static const char *
target_to_str (UsbGuardTarget target)
{
        switch (target) {
        case TARGET_ALLOW:
                return "Allow";
        case TARGET_BLOCK:
                return "Block";
        case TARGET_REJECT:
                return "Reject";
        default:
                g_warning ("Unknown Target: %d", target);
                return "Unknown!";
        }
}

static const char *
protection_level_to_str (GDesktopUsbProtection level)
{
        switch (level) {
        case G_DESKTOP_USB_PROTECTION_ALWAYS:
                return "Always";
        case G_DESKTOP_USB_PROTECTION_LOCKSCREEN:
                return "Lockscreen";
        default:
                g_warning ("Unknown Protection Level: %d", level);
                return "Unknown!";
        }
}


static void
add_usbguard_allow_rule (GsdUsbProtectionManager *manager)
{
        /* This appends a "allow all" rule.
         * It has the purpose of ensuring the authorization of new devices when
         * the lockscreen is off while respecting existing rules.
         * We make it temporary, so that we are stateless and don't alter the
         * existing (persistent) configuration.
         */

        GVariant *params;
        gboolean temporary;
        GDBusProxy *policy_proxy = manager->usb_protection_policy;

        if (policy_proxy == NULL) {
                g_warning ("Cannot add allow rule, because dbus proxy is missing");
                return;
        }

        g_debug ("Adding rule %u", USBGUARD_LAST_RULE_ID);

        temporary = TRUE;
        params = g_variant_new ("(sub)", ALLOW_ALL, USBGUARD_LAST_RULE_ID, temporary);
        g_dbus_proxy_call (policy_proxy,
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
        g_autoptr(GVariantIter) iter = NULL;
        g_autofree gchar *value = NULL;
        guint number = 0;

        g_debug ("Detecting rule...");

        g_variant_get (rules, "a(us)", &iter);
        g_return_val_if_fail (iter != NULL, FALSE);
        while (g_variant_iter_loop (iter, "(us)", &number, &value)) {
                if (g_strcmp0 (value, ALLOW_ALL) == 0) {
                        g_debug ("Detected rule!");
                        return TRUE;
                }
        }
        g_debug ("Rule not present");
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
        if (result == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Failed to fetch USBGuard rules list: %s", error->message);
                return;
        }

        rules = g_variant_get_child_value (result, 0);
        g_variant_unref (result);
        g_return_if_fail (rules != NULL);
        if (!is_usbguard_allow_rule_present (rules))
                add_usbguard_allow_rule (user_data);
}

static void
usbguard_ensure_allow_rule (GsdUsbProtectionManager *manager)
{
        GVariant *params;
        GDBusProxy *policy_proxy = manager->usb_protection_policy;

        if (policy_proxy == NULL) {
                g_warning ("Cannot list rules, because dbus proxy is missing");
                return;
        }

        /* listRules parameter is a label for matching rules.
         * We list all rules to find an "allow all" rule. */
        params = g_variant_new ("(s)", "");
        g_dbus_proxy_call (policy_proxy,
                           LIST_RULES,
                           params,
                           G_DBUS_CALL_FLAGS_NONE,
                           -1,
                           manager->cancellable,
                           usbguard_listrules_cb,
                           manager);
}

static void
settings_changed_callback (GSettings               *settings,
                           const char              *key,
                           GsdUsbProtectionManager *manager)
{
        gboolean usbguard_controlled;
        GDesktopUsbProtection protection_level;

        /* We react only if one of the two USB related properties has been changed */
        if (g_strcmp0 (key, USB_PROTECTION) != 0 && g_strcmp0 (key, USB_PROTECTION_LEVEL) != 0)
                return;

        usbguard_controlled = g_settings_get_boolean (settings, USB_PROTECTION);
        protection_level = g_settings_get_enum (settings, USB_PROTECTION_LEVEL);
        g_debug ("USBGuard control is currently %i with a protection level of %s",
                 usbguard_controlled, protection_level_to_str (protection_level));
        sync_usb_protection (manager);
}

/**
 * update_usb_protection_store:
 *
 * compares the state contained in the signal with the internal state.
 *
 * If they don't match, the GNOME USB Protection is disabled.
 * More precisely, it checks whether Inserted Device policy was changed from block to apply-policy.
 */
static void
update_usb_protection_store (GsdUsbProtectionManager *manager,
                             GVariant                *parameter)
{
        const gchar *key;
        gboolean usbguard_controlled;
        GDesktopUsbProtection protection_level;
        GSettings *settings = manager->settings;

        /* If we are not handling USBGuard configuration (e.g. the user is using
         * a third party program) we do nothing when the config changes. */
        usbguard_controlled = g_settings_get_boolean (settings, USB_PROTECTION);
        if (!usbguard_controlled)
                return;

        /* If the USBGuard configuration has been changed and doesn't match
         * our internal state, most likely means that the user externally
         * changed it. When this happens we set to false the control value. */
        key = g_variant_get_string (parameter, NULL);
        protection_level = g_settings_get_enum (settings, USB_PROTECTION_LEVEL);
        if ((g_strcmp0 (key, APPLY_POLICY) == 0 && protection_level == G_DESKTOP_USB_PROTECTION_ALWAYS)) {
                g_settings_set (settings, USB_PROTECTION, "b", FALSE);
                g_warning ("We do not control USBGuard any longer because the configuration changed externally.");
        }
}

static gboolean
is_protection_active (GsdUsbProtectionManager *manager)
{
        GSettings *settings = manager->settings;

        return g_settings_get_boolean (settings, USB_PROTECTION);
}

static void
on_notification_closed (NotifyNotification      *n,
                        GsdUsbProtectionManager *manager)
{
        g_debug ("Clearing notification");
        g_clear_object (&manager->notification);
}

static void
show_notification (GsdUsbProtectionManager *manager,
                   const char              *summary,
                   const char              *body)
{
        /* Don't show a notice if one is already displayed */
        if (manager->notification != NULL) {
                g_debug ("A notification already exists, we do not show a new one");
                return;
        }

        manager->notification = notify_notification_new (summary, body, NULL);
        notify_notification_set_app_name (manager->notification, _("USB Protection"));
        notify_notification_set_hint (manager->notification, "transient", g_variant_new_boolean (TRUE));
        notify_notification_set_hint_string (manager->notification, "x-gnome-privacy-scope", "system");
        notify_notification_set_hint (manager->notification, "image-path", g_variant_new_string ("drive-removable-media-symbolic"));
        notify_notification_set_timeout (manager->notification, NOTIFY_EXPIRES_DEFAULT);
        notify_notification_set_urgency (manager->notification, NOTIFY_URGENCY_CRITICAL);
        g_signal_connect_object (manager->notification,
                                 "closed",
                                 G_CALLBACK (on_notification_closed),
                                 manager,
                                 0);
        g_debug ("Showing notification for %s: %s", summary, body);
        if (!notify_notification_show (manager->notification, NULL)) {
                g_warning ("Failed to send USB protection notification");
                g_clear_object (&manager->notification);
        }
}

static void
call_usbguard_dbus (GDBusProxy              *proxy,
                    GsdUsbProtectionManager *manager,
                    guint                    device_id,
                    guint                    target,
                    gboolean                 permanent)
{
        if (manager->usb_protection_devices == NULL) {
                g_warning ("Could not call USBGuard, because DBus is missing");
                return;
        }

        g_debug ("Calling applyDevicePolicy with device_id %u, target %u and permanent: %i", device_id, target, permanent);
        GVariant *params = g_variant_new ("(uub)", device_id, target, permanent);
        g_dbus_proxy_call (manager->usb_protection_devices,
                           APPLY_DEVICE_POLICY,
                           params,
                           G_DBUS_CALL_FLAGS_NONE,
                           -1,
                           manager->cancellable,
                           dbus_call_log_error,
                           "Error calling USBGuard DBus to authorize a device");
}

static gboolean
is_hid_or_hub (GVariant *device)
{
        g_autoptr(GVariant) attrs = NULL;
        g_auto(GStrv) interfaces_splitted = NULL;
        g_autofree gchar *value = NULL;
        guint i;
        gboolean is_hid_or_hub = FALSE;
        gboolean has_other_classes = FALSE;

        attrs = g_variant_get_child_value (device, POLICY_APPLIED_ATTRIBUTES);
        g_return_val_if_fail (attrs != NULL, FALSE);

        if (!g_variant_lookup (attrs, WITH_INTERFACE, "s", &value))
                return FALSE;

        interfaces_splitted = g_strsplit (value, " ", -1);
        for (i = 0; i < g_strv_length (interfaces_splitted); i++) {
                if (g_str_has_prefix (interfaces_splitted[i], "03:") ||
                    g_str_has_prefix (interfaces_splitted[i], "09:")) {
                        is_hid_or_hub = TRUE;
                } else {
                        has_other_classes = TRUE;
                }
        }

        g_debug ("Device is HID or HUB: %d, has other classes: %d", is_hid_or_hub, has_other_classes);

        return is_hid_or_hub && !has_other_classes;
}

static GHashTable *
create_braille_devices_table (void)
{
        GHashTable *table;
        guint i;

        table = g_hash_table_new (g_direct_hash, g_direct_equal);

        for (i = 0; i < G_N_ELEMENTS (gsd_usb_protection_braille_devices); i++)
                g_hash_table_add (table, GUINT_TO_POINTER (gsd_usb_protection_braille_devices[i]));

        return table;
}

static gboolean
has_braille_id (GsdUsbProtectionManager *manager,
                GVariant                *device)
{
        g_autoptr(GVariant) attrs = NULL;
        g_autofree gchar *id_value = NULL;
        g_auto(GStrv) parts = NULL;
        gchar *endptr = NULL;
        guint vendor_id = 0;
        guint product_id = 0;
        guint32 combined_id;

        g_return_val_if_fail (manager->braille_devices != NULL, FALSE);

        attrs = g_variant_get_child_value (device, POLICY_APPLIED_ATTRIBUTES);
        g_return_val_if_fail (attrs != NULL, FALSE);

        if (!g_variant_lookup (attrs, "id", "s", &id_value))
                return FALSE;

        parts = g_strsplit (id_value, ":", 2);
        if (!parts || g_strv_length (parts) != 2)
                return FALSE;

        vendor_id = g_ascii_strtoull (parts[0], &endptr, 16);
        if (*endptr != '\0')
                return FALSE;

        product_id = g_ascii_strtoull (parts[1], &endptr, 16);
        if (*endptr != '\0')
                return FALSE;

        combined_id = (vendor_id << 16) | product_id;
        return g_hash_table_contains (manager->braille_devices, GUINT_TO_POINTER (combined_id));
}

static gboolean
has_braille_classes (GVariant *device)
{
        g_autoptr(GVariant) attrs = NULL;
        g_auto(GStrv) interfaces = NULL;
        g_autofree gchar *value = NULL;
        guint i;

        attrs = g_variant_get_child_value (device, POLICY_APPLIED_ATTRIBUTES);
        g_return_val_if_fail (attrs != NULL, FALSE);

        if (!g_variant_lookup (attrs, WITH_INTERFACE, "s", &value))
                return FALSE;

        interfaces = g_strsplit (value, " ", -1);
        for (i = 0; i < g_strv_length (interfaces); i++) {
                /* HID class (03:*) */
                if (g_str_has_prefix (interfaces[i], "03:"))
                        continue;
                /* Audio class (01:*) */
                if (g_str_has_prefix (interfaces[i], "01:"))
                        continue;
                /* CDC ACM (02:02:*) */
                if (g_str_has_prefix (interfaces[i], "02:02:"))
                        continue;
                /* Vendor specific (ff:*) */
                if (g_ascii_strncasecmp (interfaces[i], "ff:", 3) == 0)
                        continue;

                /* Interface doesn't match any allowed braille class */
                return FALSE;
        }

        return TRUE;
}

static gboolean
check_braille (GsdUsbProtectionManager *manager,
               GVariant                *device)
{
        /* Only consider devices that have a valid ID and only valid classes  */
        return has_braille_id (manager, device) &&
               has_braille_classes (device);
}

static gboolean
is_hardwired (GVariant *device)
{
        g_autoptr(GVariant) attrs = NULL;
        g_autofree gchar *value = NULL;

        attrs = g_variant_get_child_value (device, POLICY_APPLIED_ATTRIBUTES);
        g_return_val_if_fail (attrs != NULL, FALSE);

        if (!g_variant_lookup (attrs, WITH_CONNECT_TYPE, "s", &value))
                return FALSE;

        return g_strcmp0 (value, "hardwired") == 0;
}

static void
authorize_device (GsdUsbProtectionManager *manager,
                  guint                    device_id)
{
        g_return_if_fail (manager->usb_protection_devices != NULL);

        g_debug ("Authorizing device %u", device_id);
        call_usbguard_dbus (manager->usb_protection_devices,
                            manager,
                            device_id,
                            TARGET_ALLOW,
                            FALSE);
}

typedef struct {
        GsdUsbProtectionManager *manager;
        guint device_id;
} ManagerDeviceId;

static void
on_screen_locked (GsdScreenSaver  *screen_saver,
                  GAsyncResult    *result,
                  ManagerDeviceId *manager_devid)
{
        gboolean ret;
        g_autoptr(GError) error = NULL;
        GsdUsbProtectionManager *manager = manager_devid->manager;
        guint device_id = manager_devid->device_id;
        g_free (manager_devid);

        ret = gsd_screen_saver_call_lock_finish (screen_saver, result, &error);
        if (!ret) {
                if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        return;
                g_warning ("Could not lock screen: %s", error->message);
        }

        authorize_device (manager, device_id);
        show_notification (manager,
                           _("New USB device"),
                           _("New USB device has been activated while the session was not locked. "
                             "If you did not intend to insert any device, "
                             "check your system for any suspicious gadgets and remove them."));
}

static void
usbguard_in_lockscreen_level (GsdUsbProtectionManager *manager,
                              GVariant                *parameters)
{
        guint device_id;
        gboolean session_is_locked = manager->session_locked;
        gboolean hid_or_hub_only = is_hid_or_hub (parameters);
        gboolean valid_braille = check_braille (manager, parameters);

        /* Allow everything when the session is unlocked. */
        if (!session_is_locked) {
                g_debug ("The session is not locked and we're in Lockscreen-only mode. "
                         "The device should get authorized by an existing USBGuard rule");
                return;
        }

        /* When session is locked, only HIDs, HUBs or braille devices without
         * any other class are allowed */
        if (hid_or_hub_only || valid_braille) {
                show_notification (manager,
                                   _("New device detected"),
                                   _("Either one of your existing devices has "
                                     "been reconnected or a new one has been "
                                     "inserted. If you did not do it, check "
                                     "your system for any suspicious device."));
                g_variant_get_child (parameters, POLICY_APPLIED_DEVICE_ID, "u", &device_id);
                authorize_device (manager, device_id);
                return;
        }

        show_notification (manager,
                           _("Reconnect USB device"),
                           _("New device has been detected while you were away. "
                             "Please disconnect and reconnect the device to "
                             "start using it."));
}

static void
usbguard_in_always_level (GsdUsbProtectionManager *manager,
                          GVariant                *parameters)
{
        guint device_id;
        gboolean session_is_locked = manager->session_locked;
        gboolean hid_or_hub_only = is_hid_or_hub (parameters);

        g_variant_get_child (parameters, POLICY_APPLIED_DEVICE_ID, "u", &device_id);

        /* Only HIDs and HUBs without any other class are allowed */

        /* Lock the screen to prevent an attacker to plug malicious
         * devices if the legitimate user forgot to lock his session. */
        if (hid_or_hub_only && !session_is_locked) {
                ManagerDeviceId *manager_devid = g_malloc (sizeof (ManagerDeviceId));
                manager_devid->manager = manager;
                manager_devid->device_id = device_id;
                gsd_screen_saver_call_lock (manager->screensaver_proxy,
                                            manager->cancellable,
                                            (GAsyncReadyCallback) on_screen_locked,
                                            manager_devid);
                return;
        }

        if (hid_or_hub_only && session_is_locked) {
                show_notification (manager,
                                   _("New device detected"),
                                   _("Either one of your existing devices has "
                                     "been reconnected or a new one has been "
                                     "inserted. If you did not do it, check "
                                     "your system for any suspicious device."));
                authorize_device (manager, device_id);
                return;
        }

        if (!hid_or_hub_only && !session_is_locked) {
                show_notification (manager,
                                _("USB device blocked"),
                                _("The new inserted device has been blocked "
                                  "because the USB protection is active. "
                                  "If you want to activate the device, disable "
                                  "the USB protection and re-insert the device."));
                return;
        }

        if (!hid_or_hub_only && session_is_locked) {
                show_notification (manager,
                                   _("USB device blocked"),
                                   _("New device has been detected while you "
                                     "were away. It has been blocked because "
                                     "the USB protection is active."));
                return;
        }
}

static void
on_usbguard_signal (GDBusProxy *proxy,
                    gchar      *sender_name,
                    gchar      *signal_name,
                    GVariant   *parameters,
                    gpointer    user_data)
{
        UsbGuardTarget target = TARGET_BLOCK;
        GDesktopUsbProtection protection_level;
        GsdUsbProtectionManager *manager = user_data;
        g_autoptr(GVariant) attrs = NULL;
        g_autofree gchar *device_name = NULL;

        g_debug ("USBGuard signal: %s", signal_name);

        /* We act only if we receive a signal indicating that a device has been inserted and a rule has been applied */
        if (g_strcmp0 (signal_name, "DevicePolicyApplied") != 0)
                return;

        g_variant_get_child (parameters, POLICY_APPLIED_TARGET, "u", &target);
        g_debug ("Device target: %s", target_to_str (target));

        /* If the device is already authorized we do nothing */
        if (target == TARGET_ALLOW) {
                guint32 rule_id;
                g_variant_get_child (parameters, POLICY_APPLIED_RULE_ID, "u", &rule_id);

                /* We would need to interject here if the allow was caused by one of our rules.
                   We're not yet putting any allow-rules into USBGuard, but we might consider
                   doing so in the future.
                 */
                g_debug ("Device will be allowed by rule %u, we return", rule_id);
                return;
        }

        /* If the USB protection is disabled we do nothing */
        if (!is_protection_active (manager)) {
                g_debug ("Protection is not active. Not acting on the device");
                return;
        }

        attrs = g_variant_get_child_value (parameters, POLICY_APPLIED_ATTRIBUTES);
        g_return_if_fail (attrs != NULL);

        if (g_variant_lookup (attrs, NAME, "s", &device_name))
                g_debug ("A new USB device has been connected: %s", device_name);

        if (is_hardwired (parameters)) {
                guint device_id;
                g_debug ("Device is hardwired, allowing it to be connected");
                g_variant_get_child (parameters, POLICY_APPLIED_DEVICE_ID, "u", &device_id);
                authorize_device (manager, device_id);
                return;
        }

        protection_level = g_settings_get_enum (manager->settings, USB_PROTECTION_LEVEL);
        g_debug ("Current protection level is %s", protection_level_to_str (protection_level));

        if (protection_level == G_DESKTOP_USB_PROTECTION_LOCKSCREEN)
                usbguard_in_lockscreen_level (manager, parameters);
        else
                usbguard_in_always_level (manager, parameters);
}


/**
 * on_usb_protection_signal:
 *
 * checks the incoming signal and eventually causes the
 * GNOME USB protection to be disabled, when the USBGuard configuration changed
 * but doesn't match the internal state.
 */
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
        g_return_if_fail (parameter != NULL);
        update_usb_protection_store (user_data, parameter);
}

static void
sync_inserted_device_policy (GObject      *source_object,
                             GAsyncResult *res,
                             gpointer      user_data)
{
        GsdUsbProtectionManager *manager;
        g_autoptr(GVariant) result = NULL;
        GSettings *settings;
        gboolean usbguard_controlled;
        GDesktopUsbProtection protection_level;
        gchar *inserted_device_policy = NULL;
        const gchar *new_policy;
        g_autoptr(GError) error = NULL;

        result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object),
                                           res,
                                           &error);
        if (result == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Failed to fetch USBGuard parameters: %s", error->message);
                return;
        }

        g_variant_get_child (result, 0, "&s", &inserted_device_policy);

        manager = GSD_USB_PROTECTION_MANAGER (user_data);
        settings = manager->settings;

        usbguard_controlled = g_settings_get_boolean (settings, USB_PROTECTION);
        protection_level = g_settings_get_enum (settings, USB_PROTECTION_LEVEL);

        if (!usbguard_controlled ||
             protection_level == G_DESKTOP_USB_PROTECTION_LOCKSCREEN) {
                /* In "Lockscreen" protection level and when we leave USBGuard
                 * configuration we add an always allow rule to make every
                 * USB device authorized. */
                usbguard_ensure_allow_rule (manager);
        }

        if (!usbguard_controlled) {
                new_policy = APPLY_POLICY;
        } else if (protection_level == G_DESKTOP_USB_PROTECTION_LOCKSCREEN) {
                new_policy = (manager->screensaver_active || manager->session_locked) ?
                             BLOCK : APPLY_POLICY;
        } else { /* G_DESKTOP_USB_PROTECTION_ALWAYS */
                new_policy = BLOCK;
        }

        g_debug ("InsertedDevicePolicy is: %s", inserted_device_policy);

        if (g_strcmp0 (inserted_device_policy, new_policy) == 0)
                return;

        /* We are out of sync. We need to call setParameter to update USBGuard state */
        if (manager->usb_protection != NULL) {
                g_debug ("Setting InsertedDevicePolicy: %s", new_policy);
                g_dbus_proxy_call (manager->usb_protection,
                                   "setParameter",
                                   g_variant_new ("(ss)",
                                                  INSERTED_DEVICE_POLICY,
                                                  new_policy),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   manager->cancellable,
                                   dbus_call_log_error,
                                   "Error calling USBGuard DBus while we were out of sync");
        }
}

static void
sync_usb_protection (GsdUsbProtectionManager *manager)
{
        GVariant *params;

        g_debug ("Attempting to sync USB parameters: %p %p",
                 manager->usb_protection_policy, manager->usb_protection);

        if (manager->usb_protection == NULL)
                return;

        params = g_variant_new ("(s)", INSERTED_DEVICE_POLICY);
        g_dbus_proxy_call (manager->usb_protection,
                           "getParameter",
                           params,
                           G_DBUS_CALL_FLAGS_NONE,
                           -1,
                           manager->cancellable,
                           sync_inserted_device_policy,
                           manager);
}

static void
usb_protection_properties_changed (GsdUsbProtectionManager *manager)
{
        GVariantBuilder props_builder;
        GVariant *props_changed = NULL;

        /* not yet connected to the session bus */
        if (manager->connection == NULL)
                return;

        g_variant_builder_init (&props_builder, G_VARIANT_TYPE ("a{sv}"));

        g_variant_builder_add (&props_builder, "{sv}", "Available",
                               g_variant_new_boolean (manager->available));

        props_changed = g_variant_new ("(s@a{sv}@as)", GSD_USB_PROTECTION_DBUS_NAME,
                                       g_variant_builder_end (&props_builder),
                                       g_variant_new_strv (NULL, 0));

        g_dbus_connection_emit_signal (manager->connection,
                                       NULL,
                                       GSD_USB_PROTECTION_DBUS_PATH,
                                       "org.freedesktop.DBus.Properties",
                                       "PropertiesChanged",
                                       props_changed, NULL);
}

static void
on_usb_protection_owner_changed_cb (GObject    *object,
                                    GParamSpec *pspec,
                                    gpointer    user_data)
{
        GsdUsbProtectionManager *manager = user_data;
        GDBusProxy *proxy = G_DBUS_PROXY (object);
        g_autofree gchar *name_owner = NULL;

        name_owner = g_dbus_proxy_get_name_owner (proxy);
        g_debug ("Got owner change: %s", name_owner);

        if (name_owner != NULL)
                manager->available = TRUE;
        else
                manager->available = FALSE;

        usb_protection_properties_changed (manager);
}

static void
handle_screensaver_active (GsdUsbProtectionManager *manager,
                           GVariant                *parameters)
{
        gboolean active;

        g_variant_get (parameters, "(b)", &active);
        g_debug ("Received screensaver ActiveChanged signal: %d (old: %d)", active, manager->screensaver_active);
        if (manager->screensaver_active == active)
                return;

        manager->screensaver_active = active;

        sync_usb_protection (manager);
}

static void
screensaver_signal_cb (GDBusProxy  *proxy,
                       const gchar *sender_name,
                       const gchar *signal_name,
                       GVariant    *parameters,
                       gpointer     user_data)
{
        g_debug ("ScreenSaver Signal: %s", signal_name);
        if (g_strcmp0 (signal_name, "ActiveChanged") == 0)
                handle_screensaver_active (GSD_USB_PROTECTION_MANAGER (user_data), parameters);
}

static void
on_session_locked (GObject    *object,
                  GParamSpec *pspec,
                  gpointer    user_data)
{
        GsdUsbProtectionManager *manager = user_data;
        gboolean session_locked;

        session_locked = gsd_session_manager_get_session_is_locked (manager->session_proxy);

        if (manager->session_locked != session_locked)
                manager->session_locked = session_locked;
}

static void
usb_protection_policy_proxy_ready (GObject      *source_object,
                                   GAsyncResult *res,
                                   gpointer      user_data)
{
        GsdUsbProtectionManager *manager;
        GDBusProxy *proxy;
        g_autoptr(GError) error = NULL;
        g_debug ("usb_protection_policy_proxy_ready");

        proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
        if (proxy == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Failed to contact USBGuard: %s", error->message);
                return;
        }

        manager = GSD_USB_PROTECTION_MANAGER (user_data);
        manager->usb_protection_policy = proxy;
        g_debug ("Set protection policy proxy to %p", proxy);
        sync_usb_protection (manager);
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
        if (proxy == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Failed to contact USBGuard: %s", error->message);
                return;
        }

        manager = GSD_USB_PROTECTION_MANAGER (user_data);
        manager->usb_protection_devices = proxy;

        /* We don't care about already plugged in devices because they'll be
         * already autorized by the "allow all" rule in USBGuard. */
        g_debug ("Listening to signals");
        g_signal_connect_object (source_object,
                                 "g-signal",
                                 G_CALLBACK (on_usbguard_signal),
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
        if (proxy == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Failed to contact USBGuard: %s", error->message);
                return;
        }

        manager = GSD_USB_PROTECTION_MANAGER (user_data);
        manager->usb_protection = proxy;

        g_signal_connect (G_OBJECT (manager->settings), "changed",
                          G_CALLBACK (settings_changed_callback), manager);

        manager->screensaver_proxy = gnome_settings_bus_get_screen_saver_proxy ();
        if (manager->screensaver_proxy == NULL) {
                g_warning ("Failed to connect to screensaver service");
                g_clear_object (&manager->usb_protection);
                return;
        }

        get_current_screen_saver_status (manager);

        g_signal_connect (manager->screensaver_proxy, "g-signal",
                          G_CALLBACK (screensaver_signal_cb), manager);

        manager->session_proxy = gnome_settings_bus_get_session_proxy ();
        if (manager->session_proxy == NULL) {
                g_warning ("Failed to connect to session service");
        } else {
                g_signal_connect (manager->session_proxy, "notify::session-is-locked",
                                  G_CALLBACK (on_session_locked), manager);
                manager->session_locked = gsd_session_manager_get_session_is_locked (manager->session_proxy);
        }

        name_owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (proxy));

        if (name_owner == NULL) {
                g_debug ("Probably USBGuard >= 0.7.5 is not currently installed.");
                manager->available = FALSE;
        } else {
                manager->available = TRUE;
        }

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

static GVariant *
handle_get_property (GDBusConnection *connection,
                     const gchar     *sender,
                     const gchar     *object_path,
                     const gchar     *interface_name,
                     const gchar     *property_name,
                     GError         **error,
                     gpointer         user_data)
{
        GsdUsbProtectionManager *manager = GSD_USB_PROTECTION_MANAGER (user_data);

        /* Check session pointer as a proxy for whether the manager is in the
           start or stop state */
        if (manager->connection == NULL)
                return NULL;

        if (g_strcmp0 (property_name, "Available") == 0)
                return g_variant_new_boolean (manager->available);

        return NULL;
}

static const GDBusInterfaceVTable interface_vtable =
{
        NULL,
        handle_get_property,
        NULL
};

static void
on_bus_gotten (GObject                 *source_object,
               GAsyncResult            *res,
               GsdUsbProtectionManager *manager)
{
        GDBusConnection *connection;
        g_autoptr(GError) error = NULL;

        connection = g_bus_get_finish (res, &error);
        if (connection == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Could not get session bus: %s", error->message);
                return;
        }

        manager->connection = connection;

        g_dbus_connection_register_object (connection,
                                           GSD_USB_PROTECTION_DBUS_PATH,
                                           manager->introspection_data->interfaces[0],
                                           &interface_vtable,
                                           manager,
                                           NULL,
                                           NULL);

        manager->name_id = g_bus_own_name_on_connection (connection,
                                                         GSD_USB_PROTECTION_DBUS_NAME,
                                                         G_BUS_NAME_OWNER_FLAGS_NONE,
                                                         NULL,
                                                         NULL,
                                                         NULL,
                                                         NULL);
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
                                  USBGUARD_DBUS_INTERFACE_VERSIONED,
                                  manager->cancellable,
                                  usb_protection_proxy_ready,
                                  manager);

        notify_init ("gnome-settings-daemon");

        manager->start_idle_id = 0;

        return FALSE;
}

static void
gsd_usb_protection_manager_startup (GApplication *app)
{
        GsdUsbProtectionManager *manager = GSD_USB_PROTECTION_MANAGER (app);

        gnome_settings_profile_start (NULL);

        manager->start_idle_id = g_idle_add ((GSourceFunc) start_usb_protection_idle_cb, manager);
        g_source_set_name_by_id (manager->start_idle_id, "[gnome-settings-daemon] start_usbguard_idle_cb");

        manager->introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
        g_assert (manager->introspection_data != NULL);

        /* Start process of owning a D-Bus name */
        g_bus_get (G_BUS_TYPE_SESSION,
                   manager->cancellable,
                   (GAsyncReadyCallback) on_bus_gotten,
                   manager);

        G_APPLICATION_CLASS (gsd_usb_protection_manager_parent_class)->startup (app);

        gnome_settings_profile_end (NULL);
}

static void
gsd_usb_protection_manager_shutdown (GApplication *app)
{
        GsdUsbProtectionManager *manager = GSD_USB_PROTECTION_MANAGER (app);

        g_debug ("Stopping USB protection manager");

        if (manager->cancellable != NULL) {
                g_cancellable_cancel (manager->cancellable);
                g_clear_object (&manager->cancellable);
        }

        g_clear_object (&manager->notification);

        if (manager->start_idle_id != 0) {
                g_source_remove (manager->start_idle_id);
                manager->start_idle_id = 0;
        }

        if (manager->name_id != 0) {
                g_bus_unown_name (manager->name_id);
                manager->name_id = 0;
        }

        g_clear_pointer (&manager->introspection_data, g_dbus_node_info_unref);
        g_clear_object (&manager->connection);
        g_clear_object (&manager->settings);
        g_clear_object (&manager->usb_protection);
        g_clear_object (&manager->usb_protection_devices);
        g_clear_object (&manager->usb_protection_policy);
        g_clear_object (&manager->screensaver_proxy);
        g_clear_object (&manager->session_proxy);
        g_clear_pointer (&manager->braille_devices, g_hash_table_destroy);

        G_APPLICATION_CLASS (gsd_usb_protection_manager_parent_class)->shutdown (app);
}

static void
gsd_usb_protection_manager_class_init (GsdUsbProtectionManagerClass *klass)
{
        GApplicationClass *application_class = G_APPLICATION_CLASS (klass);

        application_class->startup = gsd_usb_protection_manager_startup;
        application_class->shutdown = gsd_usb_protection_manager_shutdown;
}

static void
gsd_usb_protection_manager_init (GsdUsbProtectionManager *manager)
{
        manager->braille_devices = create_braille_devices_table ();
}
