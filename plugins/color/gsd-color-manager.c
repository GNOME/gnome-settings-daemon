/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2011-2013 Richard Hughes <richard@hughsie.com>
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

#include "gnome-settings-profile.h"
#include "gsd-color-calibrate.h"
#include "gsd-color-manager.h"
#include "gsd-color-profiles.h"
#include "gsd-color-state.h"
#include "gsd-night-light.h"

#define GSD_DBUS_NAME "org.gnome.SettingsDaemon"
#define GSD_DBUS_PATH "/org/gnome/SettingsDaemon"
#define GSD_DBUS_BASE_INTERFACE "org.gnome.SettingsDaemon"

#define GSD_COLOR_DBUS_NAME                     GSD_DBUS_NAME ".Color"
#define GSD_COLOR_DBUS_PATH                     GSD_DBUS_PATH "/Color"
#define GSD_COLOR_DBUS_INTERFACE                GSD_DBUS_BASE_INTERFACE ".Color"

static const gchar introspection_xml[] =
"<node>"
"  <interface name='org.gnome.SettingsDaemon.Color'>"
"    <method name='NightLightPreview'>"
"      <arg type='u' name='duration' direction='in'/>"
"    </method>"
"    <property name='NightLightActive' type='b' access='read'/>"
"    <property name='Temperature' type='u' access='readwrite'/>"
"    <property name='DisabledUntilTomorrow' type='b' access='readwrite'/>"
"    <property name='Sunrise' type='d' access='read'/>"
"    <property name='Sunset' type='d' access='read'/>"
"  </interface>"
"</node>";

struct _GsdColorManager
{
        GObject            parent;

        /* D-Bus */
        guint              name_id;
        GDBusNodeInfo     *introspection_data;
        GDBusConnection   *connection;
        GCancellable      *bus_cancellable;

        GsdColorCalibrate *calibrate;
        GsdColorProfiles  *profiles;
        GsdColorState     *state;
        GsdNightLight   *nlight;

        guint            nlight_forced_timeout_id;
};

enum {
        PROP_0,
};

static void     gsd_color_manager_class_init  (GsdColorManagerClass *klass);
static void     gsd_color_manager_init        (GsdColorManager      *color_manager);
static void     gsd_color_manager_finalize    (GObject             *object);

G_DEFINE_TYPE (GsdColorManager, gsd_color_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

GQuark
gsd_color_manager_error_quark (void)
{
        static GQuark quark = 0;
        if (!quark)
                quark = g_quark_from_static_string ("gsd_color_manager_error");
        return quark;
}

gboolean
gsd_color_manager_start (GsdColorManager *manager,
                         GError          **error)
{
        gboolean ret;

        g_debug ("Starting color manager");
        gnome_settings_profile_start (NULL);

        /* start the device probing */
        gsd_color_state_start (manager->state);

        /* start the profiles collection */
        ret = gsd_color_profiles_start (manager->profiles, error);
        if (!ret)
                goto out;
out:
        gnome_settings_profile_end (NULL);
        return ret;
}

void
gsd_color_manager_stop (GsdColorManager *manager)
{
        g_debug ("Stopping color manager");
        gsd_color_state_stop (manager->state);
        gsd_color_profiles_stop (manager->profiles);
}

static void
gsd_color_manager_class_init (GsdColorManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_color_manager_finalize;
}

static void
emit_property_changed (GsdColorManager *manager,
                       const gchar *property_name,
                       GVariant *property_value)
{
        GVariantBuilder builder;
        GVariantBuilder invalidated_builder;

        /* not yet connected */
        if (manager->connection == NULL)
                return;

        /* build the dict */
        g_variant_builder_init (&invalidated_builder, G_VARIANT_TYPE ("as"));
        g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
        g_variant_builder_add (&builder,
                               "{sv}",
                               property_name,
                               property_value);
        g_dbus_connection_emit_signal (manager->connection,
                                       NULL,
                                       GSD_COLOR_DBUS_PATH,
                                       "org.freedesktop.DBus.Properties",
                                       "PropertiesChanged",
                                       g_variant_new ("(sa{sv}as)",
                                       GSD_COLOR_DBUS_INTERFACE,
                                       &builder,
                                       &invalidated_builder),
                                       NULL);
        g_variant_builder_clear (&builder);
        g_variant_builder_clear (&invalidated_builder);
}

static void
on_active_notify (GsdNightLight *nlight,
                  GParamSpec      *pspec,
                  gpointer         user_data)
{
        GsdColorManager *manager = GSD_COLOR_MANAGER (user_data);
        emit_property_changed (manager, "NightLightActive",
                               g_variant_new_boolean (gsd_night_light_get_active (manager->nlight)));
}

static void
on_sunset_notify (GsdNightLight *nlight,
                  GParamSpec      *pspec,
                  gpointer         user_data)
{
        GsdColorManager *manager = GSD_COLOR_MANAGER (user_data);
        emit_property_changed (manager, "Sunset",
                               g_variant_new_double (gsd_night_light_get_sunset (manager->nlight)));
}

static void
on_sunrise_notify (GsdNightLight *nlight,
                   GParamSpec      *pspec,
                   gpointer         user_data)
{
        GsdColorManager *manager = GSD_COLOR_MANAGER (user_data);
        emit_property_changed (manager, "Sunrise",
                               g_variant_new_double (gsd_night_light_get_sunrise (manager->nlight)));
}

static void
on_disabled_until_tmw_notify (GsdNightLight *nlight,
                              GParamSpec      *pspec,
                              gpointer         user_data)
{
        GsdColorManager *manager = GSD_COLOR_MANAGER (user_data);
        emit_property_changed (manager, "DisabledUntilTomorrow",
                               g_variant_new_boolean (gsd_night_light_get_disabled_until_tmw (manager->nlight)));
}

static void
on_temperature_notify (GsdNightLight *nlight,
                       GParamSpec      *pspec,
                       gpointer         user_data)
{
        GsdColorManager *manager = GSD_COLOR_MANAGER (user_data);
        gdouble temperature = gsd_night_light_get_temperature (manager->nlight);
        gsd_color_state_set_temperature (manager->state, temperature);
        emit_property_changed (manager, "Temperature",
                               g_variant_new_double (temperature));
}

static void
gsd_color_manager_init (GsdColorManager *manager)
{
        /* setup calibration features */
        manager->calibrate = gsd_color_calibrate_new ();
        manager->profiles = gsd_color_profiles_new ();
        manager->state = gsd_color_state_new ();

        /* night light features */
        manager->nlight = gsd_night_light_new ();
        g_signal_connect (manager->nlight, "notify::active",
                          G_CALLBACK (on_active_notify), manager);
        g_signal_connect (manager->nlight, "notify::sunset",
                          G_CALLBACK (on_sunset_notify), manager);
        g_signal_connect (manager->nlight, "notify::sunrise",
                          G_CALLBACK (on_sunrise_notify), manager);
        g_signal_connect (manager->nlight, "notify::temperature",
                          G_CALLBACK (on_temperature_notify), manager);
        g_signal_connect (manager->nlight, "notify::disabled-until-tmw",
                          G_CALLBACK (on_disabled_until_tmw_notify), manager);
}

static void
gsd_color_manager_finalize (GObject *object)
{
        GsdColorManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_COLOR_MANAGER (object));

        manager = GSD_COLOR_MANAGER (object);

        gsd_color_manager_stop (manager);

        if (manager->bus_cancellable != NULL) {
                g_cancellable_cancel (manager->bus_cancellable);
                g_clear_object (&manager->bus_cancellable);
        }

        g_clear_pointer (&manager->introspection_data, g_dbus_node_info_unref);
        g_clear_object (&manager->connection);

        if (manager->name_id != 0) {
                g_bus_unown_name (manager->name_id);
                manager->name_id = 0;
        }

        if (manager->nlight_forced_timeout_id)
                g_source_remove (manager->nlight_forced_timeout_id);

        g_clear_object (&manager->calibrate);
        g_clear_object (&manager->profiles);
        g_clear_object (&manager->state);
        g_clear_object (&manager->nlight);

        G_OBJECT_CLASS (gsd_color_manager_parent_class)->finalize (object);
}

static gboolean
nlight_forced_timeout_cb (gpointer user_data)
{
        GsdColorManager *manager = GSD_COLOR_MANAGER (user_data);

        manager->nlight_forced_timeout_id = 0;
        gsd_night_light_set_forced (manager->nlight, FALSE);

        return G_SOURCE_REMOVE;
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
        GsdColorManager *manager = GSD_COLOR_MANAGER (user_data);

        if (g_strcmp0 (method_name, "NightLightPreview") == 0) {
                guint32 duration = 0;

                if (!manager->nlight) {
                        g_dbus_method_invocation_return_error_literal (invocation,
                                                                       G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
                                                                       "Night-light is currently unavailable");

                        return;
                }

                g_variant_get (parameters, "(u)", &duration);

                if (duration == 0 || duration > 120) {
                        g_dbus_method_invocation_return_error_literal (invocation,
                                                                       G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                                                       "Duration is out of the range (0-120].");

                        return;
                }

                if (manager->nlight_forced_timeout_id)
                        g_source_remove (manager->nlight_forced_timeout_id);
                manager->nlight_forced_timeout_id = g_timeout_add_seconds (duration, nlight_forced_timeout_cb, manager);

                gsd_night_light_set_forced (manager->nlight, TRUE);

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
        GsdColorManager *manager = GSD_COLOR_MANAGER (user_data);

        if (g_strcmp0 (interface_name, GSD_COLOR_DBUS_INTERFACE) != 0) {
                g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                             "No such interface: %s", interface_name);
                return NULL;
        }

        if (g_strcmp0 (property_name, "NightLightActive") == 0)
                return g_variant_new_boolean (gsd_night_light_get_active (manager->nlight));

        if (g_strcmp0 (property_name, "Temperature") == 0) {
                guint temperature;
                temperature = gsd_color_state_get_temperature (manager->state);
                return g_variant_new_uint32 (temperature);
        }

        if (g_strcmp0 (property_name, "DisabledUntilTomorrow") == 0)
                return g_variant_new_boolean (gsd_night_light_get_disabled_until_tmw (manager->nlight));

        if (g_strcmp0 (property_name, "Sunrise") == 0)
                return g_variant_new_double (gsd_night_light_get_sunrise (manager->nlight));

        if (g_strcmp0 (property_name, "Sunset") == 0)
                return g_variant_new_double (gsd_night_light_get_sunset (manager->nlight));

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
        GsdColorManager *manager = GSD_COLOR_MANAGER (user_data);

        if (g_strcmp0 (interface_name, GSD_COLOR_DBUS_INTERFACE) != 0) {
                g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                             "No such interface: %s", interface_name);
                return FALSE;
        }

        if (g_strcmp0 (property_name, "Temperature") == 0) {
                guint32 temperature;
                g_variant_get (value, "u", &temperature);
                if (temperature < GSD_COLOR_TEMPERATURE_MIN) {
                        g_set_error (error,
                                     G_IO_ERROR,
                                     G_IO_ERROR_INVALID_ARGUMENT,
                                     "%" G_GUINT32_FORMAT "K is < min %" G_GUINT32_FORMAT "K",
                                     temperature, GSD_COLOR_TEMPERATURE_MIN);
                        return FALSE;
                }
                if (temperature > GSD_COLOR_TEMPERATURE_MAX) {
                        g_set_error (error,
                                     G_IO_ERROR,
                                     G_IO_ERROR_INVALID_ARGUMENT,
                                     "%" G_GUINT32_FORMAT "K is > max %" G_GUINT32_FORMAT "K",
                                     temperature, GSD_COLOR_TEMPERATURE_MAX);
                        return FALSE;
                }
                gsd_color_state_set_temperature (manager->state, temperature);
                return TRUE;
        }

        if (g_strcmp0 (property_name, "DisabledUntilTomorrow") == 0) {
                gsd_night_light_set_disabled_until_tmw (manager->nlight,
                                                        g_variant_get_boolean (value));
                return TRUE;
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
on_bus_gotten (GObject             *source_object,
               GAsyncResult        *res,
               GsdColorManager     *manager)
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
                                           GSD_COLOR_DBUS_PATH,
                                           manager->introspection_data->interfaces[0],
                                           &interface_vtable,
                                           manager,
                                           NULL,
                                           NULL);

        manager->name_id = g_bus_own_name_on_connection (connection,
                                                      GSD_COLOR_DBUS_NAME,
                                                      G_BUS_NAME_OWNER_FLAGS_NONE,
                                                      NULL,
                                                      name_lost_handler_cb,
                                                      manager,
                                                      NULL);

        /* setup night light module */
        if (!gsd_night_light_start (manager->nlight, &error)) {
                g_warning ("Could not start night light module: %s", error->message);
                g_error_free (error);
        }
}

static void
register_manager_dbus (GsdColorManager *manager)
{
        manager->introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
        g_assert (manager->introspection_data != NULL);
        manager->bus_cancellable = g_cancellable_new ();

        g_bus_get (G_BUS_TYPE_SESSION,
                   manager->bus_cancellable,
                   (GAsyncReadyCallback) on_bus_gotten,
                   manager);
}

GsdColorManager *
gsd_color_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_COLOR_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
                register_manager_dbus (manager_object);
        }

        return GSD_COLOR_MANAGER (manager_object);
}
