/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
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
#include <gio/gio.h>
#include <stdlib.h>
#include <libnotify/notify.h>
#include <glib/gi18n.h>

static GDBusNodeInfo *npn_introspection_data = NULL;
static GDBusNodeInfo *pdi_introspection_data = NULL;

#define SCP_DBUS_NPN_NAME      "com.redhat.NewPrinterNotification"
#define SCP_DBUS_NPN_PATH      "/com/redhat/NewPrinterNotification"
#define SCP_DBUS_NPN_INTERFACE "com.redhat.NewPrinterNotification"

#define SCP_DBUS_PDI_NAME      "com.redhat.PrinterDriversInstaller"
#define SCP_DBUS_PDI_PATH      "/com/redhat/PrinterDriversInstaller"
#define SCP_DBUS_PDI_INTERFACE "com.redhat.PrinterDriversInstaller"

#define PACKAGE_KIT_BUS "org.freedesktop.PackageKit"
#define PACKAGE_KIT_PATH "/org/freedesktop/PackageKit"
#define PACKAGE_KIT_IFACE "org.freedesktop.PackageKit.Modify"

static const gchar npn_introspection_xml[] =
  "<node name='/com/redhat/NewPrinterNotification'>"
  "  <interface name='com.redhat.NewPrinterNotification'>"
  "    <method name='GetReady'>"
  "    </method>"
  "    <method name='NewPrinter'>"
  "      <arg type='i' name='status' direction='in'/>"
  "      <arg type='s' name='name' direction='in'/>"
  "      <arg type='s' name='mfg' direction='in'/>"
  "      <arg type='s' name='mdl' direction='in'/>"
  "      <arg type='s' name='des' direction='in'/>"
  "      <arg type='s' name='cmd' direction='in'/>"
  "    </method>"
  "  </interface>"
  "</node>";

static const gchar pdi_introspection_xml[] =
  "<node name='/com/redhat/PrinterDriversInstaller'>"
  "  <interface name='com.redhat.PrinterDriversInstaller'>"
  "    <method name='InstallDrivers'>"
  "      <arg type='s' name='mfg' direction='in'/>"
  "      <arg type='s' name='mdl' direction='in'/>"
  "      <arg type='s' name='cmd' direction='in'/>"
  "    </method>"
  "  </interface>"
  "</node>";

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
        gchar *primary_text = NULL;
        gchar *secondary_text = NULL;
        gchar *name = NULL;
        gchar *mfg = NULL;
        gchar *mdl = NULL;
        gchar *des = NULL;
        gchar *cmd = NULL;
        gchar *device = NULL;
        gint   status = 0;

        if (g_strcmp0 (method_name, "GetReady") == 0) {
                /* Translators: We are configuring new printer */
                primary_text = g_strdup (_("Configuring new printer"));
                /* Translators: Just wait */
                secondary_text = g_strdup (_("Please wait..."));

                g_dbus_method_invocation_return_value (invocation,
                                                       NULL);
        }
        else if (g_strcmp0 (method_name, "NewPrinter") == 0) {
                if (g_variant_n_children (parameters) == 6) {
                        g_variant_get (parameters, "(i&s&s&s&s&s)",
                               &status,
                               &name,
                               &mfg,
                               &mdl,
                               &des,
                               &cmd);
                }

                if (g_strrstr (name, "/")) {
                        /* name is a URI, no queue was generated, because no suitable
                         * driver was found
                         */
                        /* Translators: We have no driver installed for this printer */
                        primary_text = g_strdup (_("Missing printer driver"));

                        if ((mfg && mdl) || des) {
                                if (mfg && mdl)
                                        device = g_strdup_printf ("%s %s", mfg, mdl);
                                else
                                        device = g_strdup (des);

                                /* Translators: We have no driver installed for the device */
                                secondary_text = g_strdup_printf (_("No printer driver for %s."), device);
                                g_free (device);
                        }
                        else
                                /* Translators: We have no driver installed for this printer */
                                secondary_text = g_strdup (_("No driver for this printer."));
                }
                else {
                        /* name is the name of the queue which hal_lpadmin has set up
                         * automatically.
                         */

                        /* TODO: handle missing packages as system-config-printer does
                         */
                }

                g_dbus_method_invocation_return_value (invocation,
                                                       NULL);
        }
        else if (g_strcmp0 (method_name, "InstallDrivers") == 0) {
                GDBusProxy *proxy;
                GError     *error = NULL;

                if (g_variant_n_children (parameters) == 3) {
                        g_variant_get (parameters, "(&s&s&s)",
                               &mfg,
                               &mdl,
                               &cmd);
                }

                if (mfg && mdl)
                        device = g_strdup_printf ("MFG:%s;MDL:%s;", mfg, mdl);

                proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                       G_DBUS_PROXY_FLAGS_NONE,
                                                       NULL,
                                                       PACKAGE_KIT_BUS,
                                                       PACKAGE_KIT_PATH,
                                                       PACKAGE_KIT_IFACE,
                                                       NULL,
                                                       &error);

                if (proxy && device) {
                        GVariantBuilder *builder = NULL;
                        GVariant        *ipd_parameters = NULL;

                        builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));
                        g_variant_builder_add (builder, "s", device);

                        ipd_parameters = g_variant_new ("(uass)",
                                                        0,
                                                        builder,
                                                        "hide-finished");

                        g_dbus_proxy_call_sync (proxy,
                                                "InstallPrinterDrivers",
                                                ipd_parameters,
                                                G_DBUS_CALL_FLAGS_NONE,
                                                3600000,
                                                NULL,
                                                &error);

                        if (error)
                                g_warning ("%s", error->message);

                        g_variant_unref (ipd_parameters);
                        g_variant_builder_unref (builder);
                        g_object_unref (proxy);
                        g_clear_error (&error);
                }

                g_dbus_method_invocation_return_value (invocation,
                                                       NULL);
        }

        if (primary_text) {
                NotifyNotification *notification;
                notification = notify_notification_new (primary_text,
                                                        secondary_text,
                                                        "printer-symbolic");
                notify_notification_set_app_name (notification, _("Printers"));
                notify_notification_set_hint (notification, "transient", g_variant_new_boolean (TRUE));

                notify_notification_show (notification, NULL);
                g_object_unref (notification);
                g_free (primary_text);
                g_free (secondary_text);
        }
}

static const GDBusInterfaceVTable interface_vtable =
{
  handle_method_call,
  NULL,
  NULL
};

static void
on_npn_bus_acquired (GDBusConnection *connection,
                     const gchar     *name,
                     gpointer         user_data)
{
  guint registration_id;

  registration_id = g_dbus_connection_register_object (connection,
                                                       SCP_DBUS_NPN_PATH,
                                                       npn_introspection_data->interfaces[0],
                                                       &interface_vtable,
                                                       NULL,
                                                       NULL,
                                                       NULL);
  g_assert (registration_id > 0);
}

static void
on_pdi_bus_acquired (GDBusConnection *connection,
                     const gchar     *name,
                     gpointer         user_data)
{
  guint registration_id;

  registration_id = g_dbus_connection_register_object (connection,
                                                       SCP_DBUS_PDI_PATH,
                                                       pdi_introspection_data->interfaces[0],
                                                       &interface_vtable,
                                                       NULL,
                                                       NULL,
                                                       NULL);
  g_assert (registration_id > 0);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  exit (1);
}

int
main (int argc, char *argv[])
{
  guint npn_owner_id;
  guint pdi_owner_id;
  GMainLoop *loop;

  g_type_init ();

  notify_init ("gnome-settings-daemon-printer");

  npn_introspection_data =
          g_dbus_node_info_new_for_xml (npn_introspection_xml, NULL);
  g_assert (npn_introspection_data != NULL);

  pdi_introspection_data =
          g_dbus_node_info_new_for_xml (pdi_introspection_xml, NULL);
  g_assert (pdi_introspection_data != NULL);

  npn_owner_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
                                 SCP_DBUS_NPN_NAME,
                                 G_BUS_NAME_OWNER_FLAGS_NONE,
                                 on_npn_bus_acquired,
                                 on_name_acquired,
                                 on_name_lost,
                                 NULL,
                                 NULL);

  pdi_owner_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
                                 SCP_DBUS_PDI_NAME,
                                 G_BUS_NAME_OWNER_FLAGS_NONE,
                                 on_pdi_bus_acquired,
                                 on_name_acquired,
                                 on_name_lost,
                                 NULL,
                                 NULL);

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  g_bus_unown_name (npn_owner_id);
  g_bus_unown_name (pdi_owner_id);

  g_dbus_node_info_unref (npn_introspection_data);
  g_dbus_node_info_unref (pdi_introspection_data);

  return 0;
}
