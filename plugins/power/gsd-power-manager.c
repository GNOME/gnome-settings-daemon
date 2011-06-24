/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2011 Richard Hughes <richard@hughsiec
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <canberra-gtk.h>
#include <libupower-glib/upower.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-rr.h>

#include "gnome-settings-profile.h"
#include "gsd-enums.h"
#include "gsd-power-manager.h"

#define GNOME_SESSION_DBUS_NAME                 "org.gnome.SessionManager"
#define GNOME_SESSION_DBUS_PATH                 "/org/gnome/SessionManager"
#define GNOME_SESSION_DBUS_INTERFACE            "org.gnome.SessionManager"

#define CONSOLEKIT_DBUS_NAME                    "org.freedesktop.ConsoleKit"
#define CONSOLEKIT_DBUS_PATH_MANAGER            "/org/freedesktop/ConsoleKit/Manager"
#define CONSOLEKIT_DBUS_INTERFACE_MANAGER       "org.freedesktop.ConsoleKit.Manager"

#define UPOWER_DBUS_NAME                        "org.freedesktop.UPower"
#define UPOWER_DBUS_PATH_KBDBACKLIGHT           "/org/freedesktop/UPower/KbdBacklight"
#define UPOWER_DBUS_INTERFACE_KBDBACKLIGHT      "org.freedesktop.UPower.KbdBacklight"

#define GSD_POWER_SETTINGS_SCHEMA               "org.gnome.settings-daemon.plugins.power"

#define GSD_DBUS_PATH                           "/org/gnome/SettingsDaemon"
#define GSD_POWER_DBUS_PATH                     GSD_DBUS_PATH "/Power"
#define GSD_POWER_DBUS_INTERFACE_SCREEN         "org.gnome.SettingsDaemon.Power.Screen"
#define GSD_POWER_DBUS_INTERFACE_KEYBOARD       "org.gnome.SettingsDaemon.Power.Keyboard"

static const gchar introspection_xml[] =
"<node>"
"  <interface name='org.gnome.SettingsDaemon.Power.Screen'>"
"    <method name='StepUp'>"
"      <arg type='u' name='new_percentage' direction='out'/>"
"    </method>"
"    <method name='StepDown'>"
"      <arg type='u' name='new_percentage' direction='out'/>"
"    </method>"
"    <method name='GetPercentage'>"
"      <arg type='u' name='percentage' direction='out'/>"
"    </method>"
"    <method name='SetPercentage'>"
"      <arg type='u' name='percentage' direction='in'/>"
"      <arg type='u' name='new_percentage' direction='out'/>"
"    </method>"
"  </interface>"
"  <interface name='org.gnome.SettingsDaemon.Power.Keyboard'>"
"    <method name='StepUp'>"
"      <arg type='u' name='new_percentage' direction='out'/>"
"    </method>"
"    <method name='StepDown'>"
"      <arg type='u' name='new_percentage' direction='out'/>"
"    </method>"
"    <method name='Toggle'>"
"      <arg type='u' name='new_percentage' direction='out'/>"
"    </method>"
"  </interface>"
"</node>";

/* on ACPI machines we have 4-16 levels, on others it's ~150 */
#define BRIGHTNESS_STEP_AMOUNT(max) (max < 20 ? 1 : max / 20)

/* take a discrete value with offset and convert to percentage */
#define ABS_TO_PERCENTAGE(min, max, value) (((value - min) * 100) / (max - min))
#define PERCENTAGE_TO_ABS(min, max, value) (min + (((max - min) * value) / 100))

#define GSD_POWER_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_POWER_MANAGER, GsdPowerManagerPrivate))

struct GsdPowerManagerPrivate
{
        gboolean                 lid_is_closed;
        GSettings               *settings;
        UpClient                *up_client;
        GDBusNodeInfo           *introspection_data;
        GDBusConnection         *connection;
        GDBusProxy              *upower_kdb_proxy;
        gint                     kbd_brightness_now;
        gint                     kbd_brightness_max;
        gint                     kbd_brightness_old;
        GnomeRRScreen           *x11_screen;
};

enum {
        PROP_0,
};

static void     gsd_power_manager_class_init  (GsdPowerManagerClass *klass);
static void     gsd_power_manager_init        (GsdPowerManager      *power_manager);
static void     gsd_power_manager_finalize    (GObject              *object);

G_DEFINE_TYPE (GsdPowerManager, gsd_power_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

GQuark
gsd_power_manager_error_quark (void)
{
        static GQuark quark = 0;
        if (!quark)
                quark = g_quark_from_static_string ("gsd_power_manager_error");
        return quark;
}

static void
gnome_session_shutdown_cb (GObject *source_object,
                           GAsyncResult *res,
                           gpointer user_data)
{
        GVariant *result;
        GError *error = NULL;

        result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object),
                                           res,
                                           &error);
        if (result == NULL) {
                g_warning ("couldn't shutdown using gnome-session: %s",
                           error->message);
                g_error_free (error);
        } else {
                g_variant_unref (result);
        }
}

static void
gnome_session_shutdown (void)
{
        GError *error = NULL;
        GDBusProxy *proxy;

        /* ask gnome-session to show the shutdown dialog with a timeout */
        proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                               G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                                               NULL,
                                               GNOME_SESSION_DBUS_NAME,
                                               GNOME_SESSION_DBUS_PATH,
                                               GNOME_SESSION_DBUS_INTERFACE,
                                               NULL, &error);
        if (proxy == NULL) {
                g_warning ("cannot connect to gnome-session: %s",
                           error->message);
                g_error_free (error);
                return;
        }
        g_dbus_proxy_call (proxy,
                           "Shutdown",
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           -1, NULL,
                           gnome_session_shutdown_cb, NULL);
        g_object_unref (proxy);
}

static void
consolekit_stop_cb (GObject *source_object,
                    GAsyncResult *res,
                    gpointer user_data)
{
        GVariant *result;
        GError *error = NULL;

        result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object),
                                           res,
                                           &error);
        if (result == NULL) {
                g_warning ("couldn't stop using ConsoleKit: %s",
                           error->message);
                g_error_free (error);
        } else {
                g_variant_unref (result);
        }
}

static void
consolekit_stop (void)
{
        GError *error = NULL;
        GDBusProxy *proxy;

        /* power down the machine in a safe way */
        proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                               G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                                               NULL,
                                               CONSOLEKIT_DBUS_NAME,
                                               CONSOLEKIT_DBUS_PATH_MANAGER,
                                               CONSOLEKIT_DBUS_INTERFACE_MANAGER,
                                               NULL, &error);
        if (proxy == NULL) {
                g_warning ("cannot connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return;
        }
        g_dbus_proxy_call (proxy,
                           "Stop",
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           -1, NULL,
                           consolekit_stop_cb, NULL);
        g_object_unref (proxy);
}

static void
do_power_action_type (GsdPowerManager *manager,
                      GsdPowerActionType action_type)
{
        gboolean ret;
        GError *error = NULL;

        switch (action_type) {
        case GSD_POWER_ACTION_SUSPEND:
                ret = up_client_suspend_sync (manager->priv->up_client,
                                              NULL, &error);
                if (!ret) {
                        g_warning ("failed to suspend: %s",
                                   error->message);
                        g_error_free (error);
                }
                break;
        case GSD_POWER_ACTION_INTERACTIVE:
                gnome_session_shutdown ();
                break;
        case GSD_POWER_ACTION_HIBERNATE:
                ret = up_client_hibernate_sync (manager->priv->up_client,
                                                NULL, &error);
                if (!ret) {
                        g_warning ("failed to suspend: %s",
                                   error->message);
                        g_error_free (error);
                }
                break;
        case GSD_POWER_ACTION_SHUTDOWN:
                /* this is only used on critically low battery where
                 * hibernate is not available and is marginally better
                 * than just powering down the computer mid-write */
                consolekit_stop ();
        case GSD_POWER_ACTION_BLANK:
        case GSD_POWER_ACTION_NOTHING:
                break;
        }
}

static void
do_lid_open_action (GsdPowerManager *manager)
{
        gint retval;
        ca_context *context;

        /* play a sound, using sounds from the naming spec */
        context = ca_gtk_context_get_for_screen (gdk_screen_get_default ());
        retval = ca_context_play (context, 0,
                                  CA_PROP_EVENT_ID, "lid-open",
                                  /* TRANSLATORS: this is the sound description */
                                  CA_PROP_EVENT_DESCRIPTION, _("Lid has been opened"),
                                  NULL);
        if (retval < 0)
                g_warning ("failed to play: %s", ca_strerror (retval));
}

static void
do_lid_closed_action (GsdPowerManager *manager)
{
        gint retval;
        ca_context *context;
        GsdPowerActionType action_type;

        /* play a sound, using sounds from the naming spec */
        context = ca_gtk_context_get_for_screen (gdk_screen_get_default ());
        retval = ca_context_play (context, 0,
                                  CA_PROP_EVENT_ID, "lid-close",
                                  /* TRANSLATORS: this is the sound description */
                                  CA_PROP_EVENT_DESCRIPTION, _("Lid has been closed"),
                                  NULL);
        if (retval < 0)
                g_warning ("failed to play: %s", ca_strerror (retval));

        /* we have different settings depending on AC state */
        if (up_client_get_on_battery (manager->priv->up_client)) {
                action_type = g_settings_get_enum (manager->priv->settings,
                                                   "lid-close-battery-action");
        } else {
                action_type = g_settings_get_enum (manager->priv->settings,
                                                   "lid-close-ac-action");
        }

        /* check we won't melt when the lid is closed */
        if (action_type != GSD_POWER_ACTION_SUSPEND &&
            action_type != GSD_POWER_ACTION_HIBERNATE) {
                if (up_client_get_lid_force_sleep (manager->priv->up_client)) {
                        g_warning ("to prevent damage, now forcing suspend");
                        do_power_action_type (manager, GSD_POWER_ACTION_SUSPEND);
                        return;
                }
        }

        /* are we docked? */
        if (up_client_get_is_docked (manager->priv->up_client)) {
                g_debug ("ignoring lid closed action because we are docked");
                return;
        }

        /* perform policy action */
        do_power_action_type (manager, action_type);
}


static void
up_client_changed_cb (UpClient *client, GsdPowerManager *manager)
{
        gboolean tmp;

        /* same state */
        tmp = up_client_get_lid_is_closed (manager->priv->up_client);
        if (manager->priv->lid_is_closed == tmp)
                return;
        manager->priv->lid_is_closed = tmp;

        /* fake a keypress */
        if (tmp)
                do_lid_closed_action (manager);
        else
                do_lid_open_action (manager);
}

gboolean
gsd_power_manager_start (GsdPowerManager *manager,
                         GError **error)
{
        g_debug ("Starting power manager");
        gnome_settings_profile_start (NULL);

        /* coldplug the list of screens */
        manager->priv->x11_screen = gnome_rr_screen_new (gdk_screen_get_default (), error);
        if (manager->priv->x11_screen == NULL)
                return FALSE;

        gnome_settings_profile_end (NULL);
        return TRUE;
}

void
gsd_power_manager_stop (GsdPowerManager *manager)
{
        g_debug ("Stopping power manager");

        if (manager->priv->introspection_data) {
                g_dbus_node_info_unref (manager->priv->introspection_data);
                manager->priv->introspection_data = NULL;
        }

        if (manager->priv->connection != NULL) {
                g_object_unref (manager->priv->connection);
                manager->priv->connection = NULL;
        }
}

static void
gsd_power_manager_class_init (GsdPowerManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_power_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdPowerManagerPrivate));
}

static void
power_keyboard_proxy_ready_cb (GObject             *source_object,
                               GAsyncResult        *res,
                               gpointer             user_data)
{
        GVariant *k_now = NULL;
        GVariant *k_max = NULL;
        GError *error = NULL;
        GsdPowerManager *manager = GSD_POWER_MANAGER (user_data);

        manager->priv->upower_kdb_proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
        if (manager->priv->upower_kdb_proxy == NULL) {
                g_warning ("Could not connect to UPower: %s",
                           error->message);
                g_error_free (error);
                goto out;
        }

        k_now = g_dbus_proxy_call_sync (manager->priv->upower_kdb_proxy,
                                        "GetBrightness",
                                        NULL,
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        NULL,
                                        &error);
        if (k_now == NULL) {
                g_warning ("Failed to get brightness: %s", error->message);
                g_error_free (error);
                goto out;
        }

        k_max = g_dbus_proxy_call_sync (manager->priv->upower_kdb_proxy,
                                        "GetMaxBrightness",
                                        NULL,
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        NULL,
                                        &error);
        if (k_max == NULL) {
                g_warning ("Failed to get max brightness: %s", error->message);
                g_error_free (error);
                goto out;
        }

        g_variant_get (k_now, "(i)", &manager->priv->kbd_brightness_now);
        g_variant_get (k_max, "(i)", &manager->priv->kbd_brightness_max);
out:
        if (k_now != NULL)
                g_variant_unref (k_now);
        if (k_max != NULL)
                g_variant_unref (k_max);
}

static void
gsd_power_manager_init (GsdPowerManager *manager)
{
        manager->priv = GSD_POWER_MANAGER_GET_PRIVATE (manager);

        manager->priv->kbd_brightness_old = -1;
        manager->priv->settings = g_settings_new (GSD_POWER_SETTINGS_SCHEMA);
        manager->priv->up_client = up_client_new ();
        manager->priv->lid_is_closed = up_client_get_lid_is_closed (manager->priv->up_client);
        g_signal_connect (manager->priv->up_client, "changed",
                          G_CALLBACK (up_client_changed_cb), manager);

        /* connect to UPower for keyboard backlight control */
        g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                  G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                                  NULL,
                                  UPOWER_DBUS_NAME,
                                  UPOWER_DBUS_PATH_KBDBACKLIGHT,
                                  UPOWER_DBUS_INTERFACE_KBDBACKLIGHT,
                                  NULL,
                                  power_keyboard_proxy_ready_cb,
                                  manager);
}

static void
gsd_power_manager_finalize (GObject *object)
{
        GsdPowerManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_POWER_MANAGER (object));

        manager = GSD_POWER_MANAGER (object);

        g_return_if_fail (manager->priv != NULL);

        g_object_unref (manager->priv->settings);
        g_object_unref (manager->priv->up_client);
        if (manager->priv->x11_screen != NULL);
                g_object_unref (manager->priv->x11_screen);

        G_OBJECT_CLASS (gsd_power_manager_parent_class)->finalize (object);
}

static gboolean
upower_kbd_set_brightness (GsdPowerManager *manager, guint value, GError **error)
{
        GVariant *retval;

        /* same as before */
        if (manager->priv->kbd_brightness_now == value)
                return TRUE;

        /* update h/w value */
        retval = g_dbus_proxy_call_sync (manager->priv->upower_kdb_proxy,
                                         "SetBrightness",
                                         g_variant_new ("(i)", (gint) value),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL,
                                         error);
        if (retval == NULL)
                return FALSE;

        /* save new value */
        manager->priv->kbd_brightness_now = value;
        g_variant_unref (retval);
        return TRUE;
}

/* returns new level */
static void
handle_method_call_keyboard (GsdPowerManager *manager,
                             const gchar *method_name,
                             GVariant *parameters,
                             GDBusMethodInvocation *invocation)
{
        guint step;
        gint value = -1;
        gboolean ret;
        guint percentage;
        GError *error = NULL;

        if (g_strcmp0 (method_name, "StepUp") == 0) {
                g_debug ("keyboard step up");
                step = BRIGHTNESS_STEP_AMOUNT (manager->priv->kbd_brightness_max);
                value = MIN (manager->priv->kbd_brightness_now + step,
                             manager->priv->kbd_brightness_max);
                ret = upower_kbd_set_brightness (manager, value, &error);

        } else if (g_strcmp0 (method_name, "StepDown") == 0) {
                g_debug ("keyboard step down");
                step = BRIGHTNESS_STEP_AMOUNT (manager->priv->kbd_brightness_max);
                value = MAX (manager->priv->kbd_brightness_now - step, 0);
                ret = upower_kbd_set_brightness (manager, value, &error);

        } else if (g_strcmp0 (method_name, "Toggle") == 0) {
                if (manager->priv->kbd_brightness_old >= 0) {
                        g_debug ("keyboard toggle off");
                        ret = upower_kbd_set_brightness (manager,
                                                         manager->priv->kbd_brightness_old,
                                                         &error);
                        if (ret)
                                manager->priv->kbd_brightness_old = -1;
                } else {
                        g_debug ("keyboard toggle on");
                        ret = upower_kbd_set_brightness (manager, 0, &error);
                        if (ret)
                                manager->priv->kbd_brightness_old = manager->priv->kbd_brightness_now;
                }
        } else {
                g_assert_not_reached ();
        }

        /* return value */
        if (!ret) {
                g_dbus_method_invocation_return_gerror (invocation,
                                                        error);
                g_error_free (error);
        } else {
                percentage = ABS_TO_PERCENTAGE (0,
                                                manager->priv->kbd_brightness_max,
                                                value);
                g_dbus_method_invocation_return_value (invocation,
                                                       g_variant_new ("(u)",
                                                                      percentage));
        }
}

static GnomeRROutput *
get_primary_output (GsdPowerManager *manager)
{
        GnomeRROutput *output = NULL;
        GnomeRROutput **outputs;
        guint i;

        /* search all X11 outputs for the device id */
        outputs = gnome_rr_screen_list_outputs (manager->priv->x11_screen);
        if (outputs == NULL)
                goto out;

        for (i = 0; outputs[i] != NULL; i++) {
                if (gnome_rr_output_is_connected (outputs[i]) &&
                    gnome_rr_output_is_laptop (outputs[i])) {
                        output = outputs[i];
                        break;
                }
        }
out:
        return output;
}

static void
handle_method_call_screen (GsdPowerManager *manager,
                           const gchar *method_name,
                           GVariant *parameters,
                           GDBusMethodInvocation *invocation)
{
        gboolean ret = FALSE;
        gint min, max, now;
        guint step;
        gint value = -1;
        guint value_tmp;
        guint percentage;
        GnomeRROutput *output;
        GError *error = NULL;

        /* get the laptop screen only */
        output = get_primary_output (manager);
        if (output == NULL) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GSD_POWER_MANAGER_ERROR,
                                                       GSD_POWER_MANAGER_ERROR_FAILED,
                                                       "no laptop screen to control");
                return;
        }

        /* get capabilities (cached) */
        min = gnome_rr_output_get_backlight_min (output);
        max = gnome_rr_output_get_backlight_max (output);
        if (min < 0 || max < 0) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GSD_POWER_MANAGER_ERROR,
                                                       GSD_POWER_MANAGER_ERROR_FAILED,
                                                       "no xrandr backlight capability");
                return;
        }

        /* get what we are now */
        now = gnome_rr_output_get_backlight (output, &error);
        if (now < 0)
               goto out;

        step = BRIGHTNESS_STEP_AMOUNT (max - min + 1);
        if (g_strcmp0 (method_name, "GetPercentage") == 0) {
                g_debug ("screen get percentage");
                value = gnome_rr_output_get_backlight (output, &error);
                if (value >= 0)
                        ret = TRUE;

        } else if (g_strcmp0 (method_name, "SetPercentage") == 0) {
                g_debug ("screen set percentage");
                g_variant_get (parameters, "(u)", &value_tmp);
                value = PERCENTAGE_TO_ABS (min, max, value_tmp);
                ret = gnome_rr_output_set_backlight (output, value, &error);

        } else if (g_strcmp0 (method_name, "StepUp") == 0) {
                g_debug ("screen step up");
                value = MIN (now + step, max);
                ret = gnome_rr_output_set_backlight (output, value, &error);
        } else if (g_strcmp0 (method_name, "StepDown") == 0) {
                g_debug ("screen step down");
                value = MAX (now - step, 0);
                ret = gnome_rr_output_set_backlight (output, value, &error);
        } else {
                g_assert_not_reached ();
        }
out:
        /* return value */
        if (!ret) {
                g_dbus_method_invocation_return_gerror (invocation,
                                                        error);
                g_error_free (error);
        } else {
                percentage = ABS_TO_PERCENTAGE (min, max, value);
                g_dbus_method_invocation_return_value (invocation,
                                                       g_variant_new ("(u)",
                                                                      percentage));
        }
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
        GsdPowerManager *manager = (GsdPowerManager *) user_data;

        g_debug ("Calling method '%s.%s' for Power",
                 interface_name, method_name);

        if (g_strcmp0 (interface_name, GSD_POWER_DBUS_INTERFACE_SCREEN) == 0) {
                handle_method_call_screen (manager,
                                           method_name,
                                           parameters,
                                           invocation);
        } else if (g_strcmp0 (interface_name, GSD_POWER_DBUS_INTERFACE_KEYBOARD) == 0) {
                handle_method_call_keyboard (manager,
                                             method_name,
                                             parameters,
                                             invocation);
        } else {
                g_warning ("not recognised interface: %s", interface_name);
        }
}

static const GDBusInterfaceVTable interface_vtable =
{
        handle_method_call,
        NULL, /* GetProperty */
        NULL, /* SetProperty */
};

static void
on_bus_gotten (GObject             *source_object,
               GAsyncResult        *res,
               GsdPowerManager     *manager)
{
        GDBusConnection *connection;
        GDBusInterfaceInfo **infos;
        GError *error = NULL;
        guint i;

        connection = g_bus_get_finish (res, &error);
        if (connection == NULL) {
                g_warning ("Could not get session bus: %s", error->message);
                g_error_free (error);
                return;
        }
        manager->priv->connection = connection;
        infos = manager->priv->introspection_data->interfaces;
        for (i = 0; infos[i] != NULL; i++) {
                g_dbus_connection_register_object (connection,
                                                   GSD_POWER_DBUS_PATH,
                                                   infos[i],
                                                   &interface_vtable,
                                                   manager,
                                                   NULL,
                                                   NULL);
        }
}

static void
register_manager_dbus (GsdPowerManager *manager)
{
        manager->priv->introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
        g_assert (manager->priv->introspection_data != NULL);

        g_bus_get (G_BUS_TYPE_SESSION,
                   NULL,
                   (GAsyncReadyCallback) on_bus_gotten,
                   manager);
}

GsdPowerManager *
gsd_power_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_POWER_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
                register_manager_dbus (manager_object);
        }
        return GSD_POWER_MANAGER (manager_object);
}
