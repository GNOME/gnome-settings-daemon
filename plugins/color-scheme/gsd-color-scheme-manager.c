/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2024-2025 Jamie Murphy <jmurphy@gnome.org>
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
 */

#include "config.h"

#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include "gnome-settings-profile.h"
#include "gsd-color-scheme-manager.h"
#include "gsd-color-scheme.h"
#include "gsd-shell-helper.h"
#include "gnome-settings-bus.h"

#define GSD_DBUS_NAME "org.gnome.SettingsDaemon"
#define GSD_DBUS_PATH "/org/gnome/SettingsDaemon"
#define GSD_DBUS_BASE_INTERFACE "org.gnome.SettingsDaemon"

#define GSD_COLOR_SCHEME_DBUS_NAME            GSD_DBUS_NAME ".ColorScheme"
#define GSD_COLOR_SCHEME_DBUS_PATH            GSD_DBUS_PATH "/ColorScheme"
#define GSD_COLOR_SCHEME_DBUS_INTERFACE       GSD_DBUS_BASE_INTERFACE ".ColorScheme"

static const gchar introspection_xml[] =
"<node>"
"  <interface name='org.gnome.SettingsDaemon.ColorScheme'>"
"    <property name='Sunrise' type='d' access='read'/>"
"    <property name='Sunset' type='d' access='read'/>"
"  </interface>"
"</node>";

struct _GsdColorSchemeManager
{
    GApplication         parent;

    /* D-Bus */
    guint                name_id;
    GDBusNodeInfo       *introspection_data;
    GDBusConnection     *connection;
    GCancellable        *bus_cancellable;

    GsdShell            *shell_proxy;
    GsdColorScheme      *color_scheme;
};

enum {
    PROP_0,
};

static void gsd_color_scheme_manager_class_init  (GsdColorSchemeManagerClass *klass);
static void gsd_color_scheme_manager_init        (GsdColorSchemeManager      *manager);
static void gsd_color_scheme_manager_finalize    (GObject                    *object);

static void register_manager_dbus (GsdColorSchemeManager *manager);

G_DEFINE_TYPE (GsdColorSchemeManager, gsd_color_scheme_manager, G_TYPE_APPLICATION)

static void
gsd_color_scheme_manager_startup (GApplication *app)
{
    GsdColorSchemeManager *manager = GSD_COLOR_SCHEME_MANAGER (app);

    g_debug ("Starting color scheme manager");
    gnome_settings_profile_start (NULL);

    register_manager_dbus (manager);

    G_APPLICATION_CLASS (gsd_color_scheme_manager_parent_class)->startup (app);

    gnome_settings_profile_end (NULL);
}

static void
gsd_color_scheme_manager_shutdown (GApplication *app)
{
    GsdColorSchemeManager *manager = GSD_COLOR_SCHEME_MANAGER (app);

    g_debug ("Stopping color scheme manager");

    if (manager->bus_cancellable != NULL) {
        g_cancellable_cancel (manager->bus_cancellable);
        g_clear_object (&manager->bus_cancellable);
    }

    g_clear_pointer (&manager->introspection_data, g_dbus_node_info_unref);
    g_clear_object (&manager->connection);
    g_clear_handle_id (&manager->name_id, g_bus_unown_name);

    G_APPLICATION_CLASS (gsd_color_scheme_manager_parent_class)->shutdown (app);
}

static void
gsd_color_scheme_manager_class_init (GsdColorSchemeManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GApplicationClass *application_class = G_APPLICATION_CLASS (klass);

    object_class->finalize = gsd_color_scheme_manager_finalize;

    application_class->startup = gsd_color_scheme_manager_startup;
    application_class->shutdown = gsd_color_scheme_manager_shutdown;
}

static void
emit_property_changed (GsdColorSchemeManager *manager,
                       const gchar           *property_name,
                       GVariant              *property_value)
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
                                   GSD_COLOR_SCHEME_DBUS_PATH,
                                   "org.freedesktop.DBus.Properties",
                                   "PropertiesChanged",
                                   g_variant_new ("(sa{sv}as)",
                                   GSD_COLOR_SCHEME_DBUS_INTERFACE,
                                   &builder,
                                   &invalidated_builder),
                                   NULL);
    g_variant_builder_clear (&builder);
    g_variant_builder_clear (&invalidated_builder);
}

static void
on_sunset_notify (GsdColorSchemeManager *manager)
{
    emit_property_changed (manager, "Sunset",
                           g_variant_new_double (gsd_color_scheme_get_sunset (manager->color_scheme)));
}

static void
on_sunrise_notify (GsdColorSchemeManager *manager)
{
    emit_property_changed (manager, "Sunrise",
                           g_variant_new_double (gsd_color_scheme_get_sunrise (manager->color_scheme)));
}

static void
on_scheme_switched (GsdColorSchemeManager *manager)
{
    g_autoptr(GError) error = NULL;

    if (manager->shell_proxy == NULL)
        return;

    gsd_shell_call_screen_transition_sync (manager->shell_proxy, NULL, &error);

    if (error)
        g_warning ("Couldn't transition screen: %s", error->message);
}

static void
gsd_color_scheme_manager_init (GsdColorSchemeManager *manager)
{
    manager->shell_proxy = gnome_settings_bus_get_shell_proxy ();

    manager->color_scheme = gsd_color_scheme_new ();
    g_signal_connect_swapped (manager->color_scheme, "notify::sunset",
                              G_CALLBACK (on_sunset_notify), manager);
    g_signal_connect_swapped (manager->color_scheme, "notify::sunrise",
                              G_CALLBACK (on_sunrise_notify), manager);
    g_signal_connect_swapped (manager->color_scheme, "scheme-switched",
                              G_CALLBACK (on_scheme_switched), manager);
}

static void
gsd_color_scheme_manager_finalize (GObject *object)
{
    GsdColorSchemeManager *manager;

    g_return_if_fail (object != NULL);
    g_return_if_fail (GSD_IS_COLOR_SCHEME_MANAGER (object));

    manager = GSD_COLOR_SCHEME_MANAGER (object);

    g_clear_object (&manager->color_scheme);

    G_OBJECT_CLASS (gsd_color_scheme_manager_parent_class)->finalize (object);
}

static GVariant *
handle_get_property (GDBusConnection  *connection,
                     const gchar      *sender,
                     const gchar      *object_path,
                     const gchar      *interface_name,
                     const gchar      *property_name,
                     GError          **error,
                     gpointer          user_data)
{
    GsdColorSchemeManager *manager = GSD_COLOR_SCHEME_MANAGER (user_data);

    if (g_strcmp0 (interface_name, GSD_COLOR_SCHEME_DBUS_INTERFACE) != 0) {
        g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                     "No such interface: %s", interface_name);
        return NULL;
    }

    if (g_strcmp0 (property_name, "Sunrise") == 0)
        return g_variant_new_double (gsd_color_scheme_get_sunrise (manager->color_scheme));

    if (g_strcmp0 (property_name, "Sunset") == 0)
        return g_variant_new_double (gsd_color_scheme_get_sunset (manager->color_scheme));

    g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                 "Failed to get property: %s", property_name);
    return NULL;
}

static const GDBusInterfaceVTable interface_vtable =
{
    NULL, /* handle_method_call */
    handle_get_property,
    NULL /* handle_set_property */
};

static void
on_bus_gotten (GObject               *source_object,
               GAsyncResult          *res,
               GsdColorSchemeManager *manager)
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
                                       GSD_COLOR_SCHEME_DBUS_PATH,
                                       manager->introspection_data->interfaces[0],
                                       &interface_vtable,
                                       manager,
                                       NULL,
                                       NULL);

    manager->name_id = g_bus_own_name_on_connection (connection,
                                                     GSD_COLOR_SCHEME_DBUS_NAME,
                                                     G_BUS_NAME_OWNER_FLAGS_NONE,
                                                     NULL,
                                                     NULL,
                                                     NULL,
                                                     NULL);

    /* setup color scheme module */
    if (!gsd_color_scheme_start (manager->color_scheme, &error)) {
        g_warning ("Could not start color scheme module: %s", error->message);
        g_error_free (error);
    }
}

static void
register_manager_dbus (GsdColorSchemeManager *manager)
{
    manager->introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
    g_assert (manager->introspection_data != NULL);
    manager->bus_cancellable = g_cancellable_new ();

    g_bus_get (G_BUS_TYPE_SESSION,
               manager->bus_cancellable,
               (GAsyncReadyCallback) on_bus_gotten,
               manager);
}