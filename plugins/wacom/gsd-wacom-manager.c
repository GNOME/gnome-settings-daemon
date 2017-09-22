/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2010 Red Hat, Inc.
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

#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <locale.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#include "gsd-enums.h"
#include "gnome-settings-profile.h"
#include "gnome-settings-bus.h"
#include "gsd-wacom-manager.h"
#include "gsd-wacom-oled.h"
#include "gsd-shell-helper.h"
#include "gsd-device-mapper.h"
#include "gsd-device-manager.h"
#include "gsd-settings-migrate.h"


#define GSD_WACOM_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_WACOM_MANAGER, GsdWacomManagerPrivate))

#define UNKNOWN_DEVICE_NOTIFICATION_TIMEOUT 15000

#define GSD_DBUS_NAME "org.gnome.SettingsDaemon"
#define GSD_DBUS_PATH "/org/gnome/SettingsDaemon"
#define GSD_DBUS_BASE_INTERFACE "org.gnome.SettingsDaemon"

#define GSD_WACOM_DBUS_PATH GSD_DBUS_PATH "/Wacom"
#define GSD_WACOM_DBUS_NAME GSD_DBUS_NAME ".Wacom"

static const gchar introspection_xml[] =
"<node name='/org/gnome/SettingsDaemon/Wacom'>"
"  <interface name='org.gnome.SettingsDaemon.Wacom'>"
"    <method name='SetGroupModeLED'>"
"      <arg name='device_path' direction='in' type='s'/>"
"      <arg name='group' direction='in' type='u'/>"
"      <arg name='mode' direction='in' type='u'/>"
"    </method>"
"    <method name='SetOLEDLabels'>"
"      <arg name='device_path' direction='in' type='s'/>"
"      <arg name='labels' direction='in' type='as'/>"
"    </method>"
"  </interface>"
"</node>";

struct GsdWacomManagerPrivate
{
        guint start_idle_id;
        GsdDeviceManager *device_manager;
        guint device_added_id;
        guint device_removed_id;

        GsdShell *shell_proxy;

        GsdDeviceMapper *device_mapper;

        gchar *machine_id;

        /* DBus */
        GDBusNodeInfo   *introspection_data;
        GDBusConnection *dbus_connection;
        GCancellable    *dbus_cancellable;
        guint            dbus_register_object_id;
        guint            name_id;
};

static void     gsd_wacom_manager_class_init  (GsdWacomManagerClass *klass);
static void     gsd_wacom_manager_init        (GsdWacomManager      *wacom_manager);
static void     gsd_wacom_manager_finalize    (GObject              *object);

static gboolean set_led (GsdDevice  *device,
                         guint       group,
                         guint       index,
                         GError    **error);

G_DEFINE_TYPE (GsdWacomManager, gsd_wacom_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static GVariant *
map_tablet_mapping (GVariant *value)
{
        const gchar *mapping;

        mapping = g_variant_get_boolean (value) ? "absolute" : "relative";
        return g_variant_new_string (mapping);
}

static GVariant *
map_tablet_left_handed (GVariant *value)
{
        const gchar *rotation = g_variant_get_string (value, NULL);
        return g_variant_new_boolean (g_strcmp0 (rotation, "half") == 0 ||
                                      g_strcmp0 (rotation, "ccw") == 0);
}

static void
migrate_tablet_settings (GsdWacomManager *manager,
                         GsdDevice       *device)
{
        GsdSettingsMigrateEntry tablet_settings[] = {
                { "is-absolute", "mapping", map_tablet_mapping },
                { "keep-aspect", "keep-aspect", NULL },
                { "rotation", "left-handed", map_tablet_left_handed },
        };
        gchar *old_path, *new_path;
        const gchar *vendor, *product;

        gsd_device_get_device_ids (device, &vendor, &product);

        old_path = g_strdup_printf ("/org/gnome/settings-daemon/peripherals/wacom/%s-usb:%s:%s/",
                                    manager->priv->machine_id, vendor, product);
        new_path = g_strdup_printf ("/org/gnome/desktop/peripherals/tablets/%s:%s/",
                                    vendor, product);

        gsd_settings_migrate_check ("org.gnome.settings-daemon.peripherals.wacom.deprecated",
                                    old_path,
                                    "org.gnome.desktop.peripherals.tablet",
                                    new_path,
                                    tablet_settings, G_N_ELEMENTS (tablet_settings));
        g_free (old_path);
        g_free (new_path);
}

static void
gsd_wacom_manager_class_init (GsdWacomManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_wacom_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdWacomManagerPrivate));
}

static GsdDevice *
lookup_device_by_path (GsdWacomManager *manager,
                       const gchar     *path)
{
        GList *devices, *l;

        devices = gsd_device_manager_list_devices (manager->priv->device_manager,
                                                   GSD_DEVICE_TYPE_TABLET);

        for (l = devices; l; l = l->next) {
                if (g_strcmp0 (gsd_device_get_device_file (l->data),
                               path) == 0)
                        return l->data;
        }

        return NULL;
}

static void
handle_method_call (GDBusConnection       *connection,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *interface_name,
                    const gchar           *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               data)
{
	GsdWacomManager *self = GSD_WACOM_MANAGER (data);
        GError *error = NULL;
        GsdDevice *device;

        if (g_strcmp0 (method_name, "SetGroupModeLED") == 0) {
                gchar *device_path;
                guint group, mode;

		g_variant_get (parameters, "(suu)", &device_path, &group, &mode);
                device = lookup_device_by_path (self, device_path);
                if (!device) {
                        g_dbus_method_invocation_return_value (invocation, NULL);
                        return;
                }

                if (set_led (device, group, mode, &error))
                        g_dbus_method_invocation_return_value (invocation, NULL);
                else
                        g_dbus_method_invocation_return_gerror (invocation, error);
        } else if (g_strcmp0 (method_name, "SetOLEDLabels") == 0) {
                gchar *device_path, *label;
                GVariantIter *iter;
                gint i = 0;

		g_variant_get (parameters, "(sas)", &device_path, &iter);
                device = lookup_device_by_path (self, device_path);
                if (!device) {
                        g_dbus_method_invocation_return_value (invocation, NULL);
                        return;
                }

                while (g_variant_iter_loop (iter, "s", &label)) {
                        if (!set_oled (device, i, label, &error)) {
                                g_free (label);
                                break;
                        }
                        i++;
                }

                g_variant_iter_free (iter);

                if (error)
                        g_dbus_method_invocation_return_gerror (invocation, error);
                else
                        g_dbus_method_invocation_return_value (invocation, NULL);
        }
}

static const GDBusInterfaceVTable interface_vtable =
{
	handle_method_call,
	NULL, /* Get Property */
	NULL, /* Set Property */
};

static gboolean
set_led (GsdDevice  *device,
         guint       group,
	 guint       index,
         GError    **error)
{
	const char *path;
	char *command;
	gboolean ret;

#ifndef HAVE_GUDEV
	/* Not implemented on non-Linux systems */
	return TRUE;
#endif
	path = gsd_device_get_device_file (device);

	g_debug ("Switching group ID %d to index %d for device %s", group, index, path);

	command = g_strdup_printf ("pkexec " LIBEXECDIR "/gsd-wacom-led-helper --path %s --group %d --led %d",
				   path, group, index);
	ret = g_spawn_command_line_sync (command,
					 NULL,
					 NULL,
					 NULL,
					 error);
	g_free (command);

        return ret;
}

static void
device_added_cb (GsdDeviceManager *device_manager,
                 GsdDevice        *gsd_device,
                 GsdWacomManager  *manager)
{
	GsdDeviceType device_type;

	device_type = gsd_device_get_device_type (gsd_device);

        if (device_type & GSD_DEVICE_TYPE_TABLET)
                migrate_tablet_settings (manager, gsd_device);

	if ((device_type & GSD_DEVICE_TYPE_TABLET) != 0 &&
            (device_type & GSD_DEVICE_TYPE_TOUCHPAD) == 0) {
		gsd_device_mapper_add_input (manager->priv->device_mapper,
					     gsd_device);
	} else if ((device_type & GSD_DEVICE_TYPE_TOUCHSCREEN) != 0) {
		gsd_device_mapper_add_input (manager->priv->device_mapper,
					     gsd_device);
	}
}

static void
device_removed_cb (GsdDeviceManager *device_manager,
                   GsdDevice        *gsd_device,
                   GsdWacomManager  *manager)
{
	gsd_device_mapper_remove_input (manager->priv->device_mapper,
					gsd_device);
}

static void
add_devices (GsdWacomManager *manager,
             GsdDeviceType    device_type)
{
        GList *devices, *l;

        devices = gsd_device_manager_list_devices (manager->priv->device_manager,
                                                   device_type);
        for (l = devices; l ; l = l->next)
		device_added_cb (manager->priv->device_manager, l->data, manager);
        g_list_free (devices);
}

static void
set_devicepresence_handler (GsdWacomManager *manager)
{
        GsdDeviceManager *device_manager;

        device_manager = gsd_device_manager_get ();
        manager->priv->device_added_id = g_signal_connect (G_OBJECT (device_manager), "device-added",
                                                           G_CALLBACK (device_added_cb), manager);
        manager->priv->device_removed_id = g_signal_connect (G_OBJECT (device_manager), "device-removed",
                                                             G_CALLBACK (device_removed_cb), manager);
        manager->priv->device_manager = device_manager;
}

static void
gsd_wacom_manager_init (GsdWacomManager *manager)
{
        manager->priv = GSD_WACOM_MANAGER_GET_PRIVATE (manager);
}

static gboolean
gsd_wacom_manager_idle_cb (GsdWacomManager *manager)
{
        gnome_settings_profile_start (NULL);

        manager->priv->device_mapper = gsd_device_mapper_get ();

        set_devicepresence_handler (manager);

        add_devices (manager, GSD_DEVICE_TYPE_TABLET);
        add_devices (manager, GSD_DEVICE_TYPE_TOUCHSCREEN);

        gnome_settings_profile_end (NULL);

        manager->priv->start_idle_id = 0;

        return FALSE;
}

static void
on_bus_gotten (GObject		   *source_object,
	       GAsyncResult	   *res,
	       GsdWacomManager	   *manager)
{
	GDBusConnection	       *connection;
	GError		       *error = NULL;
	GsdWacomManagerPrivate *priv;

	priv = manager->priv;

	connection = g_bus_get_finish (res, &error);

	if (connection == NULL) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning ("Couldn't get session bus: %s", error->message);
		g_error_free (error);
		return;
	}

	priv->dbus_connection = connection;
	priv->dbus_register_object_id = g_dbus_connection_register_object (connection,
									   GSD_WACOM_DBUS_PATH,
									   priv->introspection_data->interfaces[0],
									   &interface_vtable,
									   manager,
									   NULL,
									   &error);

	if (priv->dbus_register_object_id == 0) {
		g_warning ("Error registering object: %s", error->message);
		g_error_free (error);
		return;
	}

        manager->priv->name_id = g_bus_own_name_on_connection (connection,
                                                               GSD_WACOM_DBUS_NAME,
                                                               G_BUS_NAME_OWNER_FLAGS_NONE,
                                                               NULL,
                                                               NULL,
                                                               NULL,
                                                               NULL);
}

static void
register_manager (GsdWacomManager *manager)
{
        manager->priv->introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
        manager->priv->dbus_cancellable = g_cancellable_new ();
        g_assert (manager->priv->introspection_data != NULL);

        g_bus_get (G_BUS_TYPE_SESSION,
                   manager->priv->dbus_cancellable,
                   (GAsyncReadyCallback) on_bus_gotten,
                   manager);
}

static gchar *
get_machine_id (void)
{
        gchar *no_per_machine_file, *machine_id = NULL;
        gboolean per_machine;
        gsize len;

        no_per_machine_file = g_build_filename (g_get_user_config_dir (), "gnome-settings-daemon", "no-per-machine-config", NULL);
        per_machine = !g_file_test (no_per_machine_file, G_FILE_TEST_EXISTS);
        g_free (no_per_machine_file);

        if (!per_machine ||
            (!g_file_get_contents ("/etc/machine-id", &machine_id, &len, NULL) &&
             !g_file_get_contents ("/var/lib/dbus/machine-id", &machine_id, &len, NULL))) {
                return g_strdup ("00000000000000000000000000000000");
        }

        machine_id[len - 1] = '\0';
        return machine_id;
}

gboolean
gsd_wacom_manager_start (GsdWacomManager *manager,
                         GError         **error)
{
        gnome_settings_profile_start (NULL);

        register_manager (manager_object);

        manager->priv->machine_id = get_machine_id ();

        manager->priv->start_idle_id = g_idle_add ((GSourceFunc) gsd_wacom_manager_idle_cb, manager);
        g_source_set_name_by_id (manager->priv->start_idle_id, "[gnome-settings-daemon] gsd_wacom_manager_idle_cb");

        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_wacom_manager_stop (GsdWacomManager *manager)
{
        GsdWacomManagerPrivate *p = manager->priv;

        g_debug ("Stopping wacom manager");

        g_clear_pointer (&manager->priv->machine_id, g_free);

        if (manager->priv->name_id != 0) {
                g_bus_unown_name (manager->priv->name_id);
                manager->priv->name_id = 0;
        }

        if (p->dbus_register_object_id) {
                g_dbus_connection_unregister_object (p->dbus_connection,
                                                     p->dbus_register_object_id);
                p->dbus_register_object_id = 0;
        }

        if (p->device_manager != NULL) {
                g_signal_handler_disconnect (p->device_manager, p->device_added_id);
                g_signal_handler_disconnect (p->device_manager, p->device_removed_id);
                p->device_manager = NULL;
        }
}

static void
gsd_wacom_manager_finalize (GObject *object)
{
        GsdWacomManager *wacom_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_WACOM_MANAGER (object));

        wacom_manager = GSD_WACOM_MANAGER (object);

        g_return_if_fail (wacom_manager->priv != NULL);

        gsd_wacom_manager_stop (wacom_manager);

        if (wacom_manager->priv->start_idle_id != 0)
                g_source_remove (wacom_manager->priv->start_idle_id);

        g_clear_object (&wacom_manager->priv->shell_proxy);

        G_OBJECT_CLASS (gsd_wacom_manager_parent_class)->finalize (object);
}

GsdWacomManager *
gsd_wacom_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_WACOM_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_WACOM_MANAGER (manager_object);
}
