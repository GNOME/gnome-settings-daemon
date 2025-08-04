/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2025 Red Hat
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

#include <glib.h>
#include <gio/gio.h>

#include "gnome-settings-systemd.h"

#define SYSTEMD_MANAGER_DBUS_NAME "org.freedesktop.systemd1"
#define SYSTEMD_MANAGER_DBUS_OBJECT "/org/freedesktop/systemd1"
#define SYSTEMD_MANAGER_DBUS_IFACE "org.freedesktop.systemd1.Manager"

gboolean
gnome_settings_have_systemd (void) {
  /* This does the same as sd_booted without needing libsystemd. */
  return g_file_test ("/run/systemd/system/", G_FILE_TEST_IS_DIR);
}

typedef struct {
        char *unit;
        gboolean running;
} ManageUnitData;

static void
manage_unit_data_free (ManageUnitData *data)
{
        g_free (data->unit);
        g_free (data);
}

static void
start_stop_cb (GObject      *source_object,
               GAsyncResult *res,
               gpointer      user_data)
{
        g_autoptr (GTask) task = G_TASK (user_data);
        g_autoptr (GError) error = NULL;
        g_autoptr (GVariant) ret = NULL;
        ManageUnitData *data = g_task_get_task_data (task);

        ret = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object),
                                             res, &error);

        if (error) {
                g_prefix_error (&error, "Failed to %s %s: ",
                                data->running ? "start" : "stop",
                                data->unit);
                g_task_return_error (task, g_steal_pointer (&error));
        } else
                g_task_return_boolean (task, TRUE);
}

static void
enable_disable_cb (GObject      *source_object,
                   GAsyncResult *res,
                   gpointer      user_data)
{
        g_autoptr (GTask) task = G_TASK (user_data);
        g_autoptr (GError) error = NULL;
        g_autoptr (GVariant) ret = NULL;
        ManageUnitData *data = g_task_get_task_data (task);

        ret = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object),
                                             res, &error);

        if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                g_warning ("Failed to %s %s: %s",
                           data->running ? "enable" : "disable",
                           data->unit,
                           error->message);
}

void
gnome_settings_systemd_manage_unit (GDBusConnection     *connection,
                                    const char          *unit,
                                    gboolean             running,
                                    gboolean             change_enabled,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
        g_autoptr (GTask) task = NULL;
        ManageUnitData *data = NULL;
        const char *unit_list[] = { unit, NULL };

        task = g_task_new (connection, cancellable, callback, user_data);

        if (!gnome_settings_have_systemd ()) {
                g_task_return_new_error (task, G_IO_ERROR,
                                         G_IO_ERROR_NOT_SUPPORTED,
                                         "Cannot manage systemd services without systemd running");
                return;
        }

        data = g_new0 (ManageUnitData, 1);
        data->unit = g_strdup (unit);
        data->running = running;
        g_task_set_task_data (task, data, (GDestroyNotify) manage_unit_data_free);

        g_dbus_connection_call (connection,
                                SYSTEMD_MANAGER_DBUS_NAME,
                                SYSTEMD_MANAGER_DBUS_OBJECT,
                                SYSTEMD_MANAGER_DBUS_IFACE,
                                running ? "StartUnit" : "StopUnit",
                                g_variant_new ("(ss)", unit, "replace"),
                                NULL,
                                G_DBUS_CALL_FLAGS_NONE,
                                -1,
                                cancellable,
                                start_stop_cb,
                                g_object_ref (task));

        if (change_enabled) {
                g_dbus_connection_call (connection,
                                        SYSTEMD_MANAGER_DBUS_NAME,
                                        SYSTEMD_MANAGER_DBUS_OBJECT,
                                        SYSTEMD_MANAGER_DBUS_IFACE,
                                        running ? "EnableUnitFilesWithFlags"
                                                : "DisableUnitFilesWithFlags",
                                        g_variant_new ("(^ast)", unit_list, 0),
                                        NULL,
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        cancellable,
                                        enable_disable_cb,
                                        g_object_ref (task));
        }
}

gboolean
gnome_settings_systemd_manage_unit_finish (GDBusConnection  *connection,
                                           GAsyncResult     *result,
                                           GError          **error)
{
        return g_task_propagate_boolean (G_TASK (result), error);
}
