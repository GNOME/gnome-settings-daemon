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
#include <math.h>

#include "gnome-settings-profile.h"
#include "gsd-color-calibrate.h"
#include "gsd-color-manager.h"
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
        GsdApplication     parent;

        /* D-Bus */
        GDBusNodeInfo     *introspection_data;

        GsdColorCalibrate *calibrate;
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
static gboolean gsd_color_manager_dbus_register (GApplication    *app,
                                                 GDBusConnection *connection,
                                                 const char      *object_path,
                                                 GError         **error);
static void     gsd_color_manager_dbus_unregister (GApplication    *app,
                                                   GDBusConnection *connection,
                                                   const char      *object_path);

G_DEFINE_TYPE (GsdColorManager, gsd_color_manager, GSD_TYPE_APPLICATION)

static void
gsd_color_manager_startup (GApplication *app)
{
        GsdColorManager *manager = GSD_COLOR_MANAGER (app);
        g_autoptr (GError) error = NULL;

        g_debug ("Starting color manager");
        gnome_settings_profile_start (NULL);

        /* start the device probing */
        gsd_color_state_start (manager->state);

        /* setup night light module */
        if (!gsd_night_light_start (manager->nlight, &error))
                g_warning ("Could not start night light module: %s", error->message);

        G_APPLICATION_CLASS (gsd_color_manager_parent_class)->startup (app);

        gnome_settings_profile_end (NULL);
}

static void
gsd_color_manager_shutdown (GApplication *app)
{
        GsdColorManager *manager = GSD_COLOR_MANAGER (app);

        g_debug ("Stopping color manager");
        gsd_color_state_stop (manager->state);

        g_clear_pointer (&manager->introspection_data, g_dbus_node_info_unref);

        g_clear_handle_id (&manager->nlight_forced_timeout_id, g_source_remove);

        G_APPLICATION_CLASS (gsd_color_manager_parent_class)->shutdown (app);
}

static void
gsd_color_manager_class_init (GsdColorManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GApplicationClass *application_class = G_APPLICATION_CLASS (klass);

        object_class->finalize = gsd_color_manager_finalize;

        application_class->startup = gsd_color_manager_startup;
        application_class->shutdown = gsd_color_manager_shutdown;
        application_class->dbus_register = gsd_color_manager_dbus_register;
        application_class->dbus_unregister = gsd_color_manager_dbus_unregister;
}

static void
emit_property_changed (GsdColorManager *manager,
                       const gchar *property_name,
                       GVariant *property_value)
{
        GVariantBuilder builder;
        GVariantBuilder invalidated_builder;
        GDBusConnection *connection = g_application_get_dbus_connection (G_APPLICATION (manager));

        /* not yet connected */
        if (connection == NULL)
                return;

        /* build the dict */
        g_variant_builder_init (&invalidated_builder, G_VARIANT_TYPE ("as"));
        g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
        g_variant_builder_add (&builder,
                               "{sv}",
                               property_name,
                               property_value);
        g_dbus_connection_emit_signal (connection,
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
                               g_variant_new_uint32 (roundf (temperature)));
}

static void
gsd_color_manager_init (GsdColorManager *manager)
{
        /* setup calibration features */
        manager->calibrate = gsd_color_calibrate_new (manager);
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

        g_clear_object (&manager->calibrate);
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

static gboolean
gsd_color_manager_dbus_register (GApplication    *app,
                                 GDBusConnection *connection,
                                 const char     *object_path,
                                 GError         **error)
{
        GsdColorManager *manager = GSD_COLOR_MANAGER (app);

        if (!G_APPLICATION_CLASS (gsd_color_manager_parent_class)->dbus_register (app,
                                                                                  connection,
                                                                                  object_path,
                                                                                  error))
                return FALSE;

        manager->introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
        g_assert (manager->introspection_data != NULL);

        g_dbus_connection_register_object (connection,
                                           GSD_COLOR_DBUS_PATH,
                                           manager->introspection_data->interfaces[0],
                                           &interface_vtable,
                                           manager,
                                           NULL,
                                           NULL);

        return TRUE;
}

static void
gsd_color_manager_dbus_unregister (GApplication    *app,
                                   GDBusConnection *connection,
                                   const char      *object_path)
{
        GsdColorManager *manager = GSD_COLOR_MANAGER (app);

        g_clear_pointer (&manager->introspection_data, g_dbus_node_info_unref);

        G_APPLICATION_CLASS (gsd_color_manager_parent_class)->dbus_unregister (app,
                                                                               connection,
                                                                               object_path);
}
