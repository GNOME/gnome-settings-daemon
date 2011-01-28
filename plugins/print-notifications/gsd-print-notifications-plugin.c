/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
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

#include <glib/gi18n-lib.h>
#include <gmodule.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus.h>

#include <cups/cups.h>
#include <libnotify/notify.h>

#include "gnome-settings-plugin.h"
#include "gsd-print-notifications-plugin.h"
#include "gsd-print-notifications-manager.h"

struct GsdPrintNotificationsPluginPrivate {
        GsdPrintNotificationsManager *manager;
        GDBusProxy                   *cups_proxy;
        GDBusConnection              *bus_connection;
        GSList                       *actual_jobs;
};

#define GSD_PRINT_NOTIFICATIONS_PLUGIN_GET_PRIVATE(object) (G_TYPE_INSTANCE_GET_PRIVATE ((object), GSD_TYPE_PRINT_NOTIFICATIONS_PLUGIN, GsdPrintNotificationsPluginPrivate))

GNOME_SETTINGS_PLUGIN_REGISTER (GsdPrintNotificationsPlugin, gsd_print_notifications_plugin)

#define CUPS_DBUS_NAME      "com.redhat.PrinterSpooler"
#define CUPS_DBUS_PATH      "/com/redhat/PrinterSpooler"
#define CUPS_DBUS_INTERFACE "com.redhat.PrinterSpooler"

static void
gsd_print_notifications_plugin_init (GsdPrintNotificationsPlugin *plugin)
{
        plugin->priv = GSD_PRINT_NOTIFICATIONS_PLUGIN_GET_PRIVATE (plugin);

        plugin->priv->manager = gsd_print_notifications_manager_new ();
}

static void
gsd_print_notifications_plugin_finalize (GObject *object)
{
        GsdPrintNotificationsPlugin *plugin;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_PRINT_NOTIFICATIONS_PLUGIN (object));

        g_debug ("GsdPrintNotificationsPlugin finalizing");

        plugin = GSD_PRINT_NOTIFICATIONS_PLUGIN (object);

        g_return_if_fail (plugin->priv != NULL);

        if (plugin->priv->manager != NULL) {
                g_object_unref (plugin->priv->manager);
        }

        G_OBJECT_CLASS (gsd_print_notifications_plugin_parent_class)->finalize (object);
}

static void
cups_notification_cb (GDBusConnection *connection,
                      const gchar     *sender_name,
                      const gchar     *object_path,
                      const gchar     *interface_name,
                      const gchar     *signal_name,
                      GVariant        *parameters,
                      gpointer         user_data)
{
  GsdPrintNotificationsPlugin *print_notifications_plugin = GSD_PRINT_NOTIFICATIONS_PLUGIN (user_data);
  NotifyNotification          *notification;
  cups_job_t                  *jobs;
  GSList                      *actual = NULL;
  GSList                      *tmp = NULL;
  gchar                       *printer_name = NULL;
  gchar                       *user_name = NULL;
  gchar                       *primary_text = NULL;
  gchar                       *secondary_text = NULL;
  guint                        job_id;
  gint                         actual_job = -1;
  gint                         num_jobs, i;
  gint                         index = -1;

  if (g_strcmp0 (signal_name, "PrinterAdded") == 0) {
    /* Translators: New printer has been added */
    primary_text = g_strdup (_("New printer"));
    if (g_variant_n_children (parameters) == 1) {
      g_variant_get (parameters, "(&s)", &printer_name);
      secondary_text = g_strdup_printf ("%s", printer_name);
    }
  }
  else if (g_strcmp0 (signal_name, "PrinterRemoved") == 0) {
    /* Translators: A printer has been removed */
    primary_text = g_strdup (_("Printer removed"));
    if (g_variant_n_children (parameters) == 1) {
      g_variant_get (parameters, "(&s)", &printer_name);
      secondary_text = g_strdup_printf ("%s", printer_name);
    }
  }
  else if (g_strcmp0 (signal_name, "QueueChanged") == 0) {
    if (g_variant_n_children (parameters) == 1 ||
        g_variant_n_children (parameters) == 3) {
      g_variant_get (parameters, "(&s)", &printer_name);
    }

    if (print_notifications_plugin->priv->actual_jobs) {
      num_jobs = cupsGetJobs (&jobs, printer_name, 1, CUPS_WHICHJOBS_ALL);

      for (actual = print_notifications_plugin->priv->actual_jobs; actual; actual = actual->next) {
        actual_job = GPOINTER_TO_INT (actual->data);
        for (i = 0; i < num_jobs; i++) {
          if (jobs[i].id == actual_job) {
            switch (jobs[i].state) {
              case IPP_JOB_PENDING:
              case IPP_JOB_HELD:
              case IPP_JOB_PROCESSING:
                break;
              case IPP_JOB_STOPPED:
                /* Translators: A print job has been stopped */
                primary_text = g_strdup (_("Print job stopped"));
                /* Translators: "print-job xy" on a printer */
                secondary_text = g_strdup_printf (_("\"%s\" on printer %s"), jobs[i].title, jobs[i].dest);
                actual->data = GINT_TO_POINTER (-1);
                break;
              case IPP_JOB_CANCELED:
                /* Translators: A print job has been canceled */
                primary_text = g_strdup (_("Print job canceled"));
                /* Translators: "print-job xy" on a printer */
                secondary_text = g_strdup_printf (_("\"%s\" on printer %s"), jobs[i].title, jobs[i].dest);
                actual->data = GINT_TO_POINTER (-1);
                break;
              case IPP_JOB_ABORTED:
                /* Translators: A print job has been aborted */
                primary_text = g_strdup (_("Print job aborted"));
                /* Translators: "print-job xy" on a printer */
                secondary_text = g_strdup_printf (_("\"%s\" on printer %s"), jobs[i].title, jobs[i].dest);
                actual->data = GINT_TO_POINTER (-1);
                break;
              case IPP_JOB_COMPLETED:
                /* Translators: A print job has been completed */
                primary_text = g_strdup (_("Print job completed"));
                /* Translators: "print-job xy" on a printer */
                secondary_text = g_strdup_printf (_("\"%s\" on printer %s"), jobs[i].title, jobs[i].dest);
                actual->data = GINT_TO_POINTER (-1);
                break;
            }
            break;
          }
        }
      }

      for (actual = print_notifications_plugin->priv->actual_jobs; actual; actual = actual->next) {
        if (GPOINTER_TO_INT (actual->data) < 0) {
          tmp = actual->next;
          print_notifications_plugin->priv->actual_jobs = 
            g_slist_delete_link (print_notifications_plugin->priv->actual_jobs, actual);
          actual = tmp;
          if (!actual)
            break;
         }
      }

      cupsFreeJobs (num_jobs, jobs);
    } 
  }
  else if (g_strcmp0 (signal_name, "JobQueuedLocal") == 0) {
    if (g_variant_n_children (parameters) == 3) {
      g_variant_get (parameters, "(&su&s)", &printer_name, &job_id, &user_name);

      num_jobs = cupsGetJobs (&jobs, printer_name, 1, CUPS_WHICHJOBS_ALL);

      index = -1;
      for (i = 0; i < num_jobs; i++)
        if (jobs[i].id == job_id)
          index = i;

      if (index >= 0) {
        /* Translators: Somebody sent something to printer */
        primary_text = g_strdup (_("New print job"));
        /* Translators: "print-job xy" on a printer */
        secondary_text = g_strdup_printf (_("\"%s\" on printer %s"), jobs[index].title, jobs[index].dest);
      }

      print_notifications_plugin->priv->actual_jobs =
        g_slist_append (print_notifications_plugin->priv->actual_jobs, GUINT_TO_POINTER (job_id));

      cupsFreeJobs (num_jobs, jobs);
    }
  }
  else if (g_strcmp0 (signal_name, "JobStartedLocal") == 0) {
    if (g_variant_n_children (parameters) == 3) {
      g_variant_get (parameters, "(&su&s)", &printer_name, &job_id, &user_name);

      for (actual = print_notifications_plugin->priv->actual_jobs; actual; actual = actual->next) {
        if (GPOINTER_TO_INT (actual->data) == job_id) {
          num_jobs = cupsGetJobs (&jobs, printer_name, 1, CUPS_WHICHJOBS_ALL);

          index = -1;
          for (i = 0; i < num_jobs; i++)
            if (jobs[i].id == job_id)
              index = i;

          if (index >= 0) {
            /* Translators: A job is printing */
            primary_text = g_strdup (_("Printing job"));
            /* Translators: "print-job xy" on a printer */
            secondary_text = g_strdup_printf (_("\"%s\" on printer %s"), jobs[index].title, jobs[index].dest);
          }

          cupsFreeJobs (num_jobs, jobs);
        }
      }
    }
  }

  if (primary_text) {
    notification = notify_notification_new (primary_text,
                                            secondary_text,
                                            NULL);
    notify_notification_show (notification, NULL);
  }
}

static void
impl_activate (GnomeSettingsPlugin *plugin)
{
        GsdPrintNotificationsPlugin *print_notifications_plugin = GSD_PRINT_NOTIFICATIONS_PLUGIN (plugin);
        cups_job_t                  *jobs;
        gboolean                     res;
        GError                      *error;
        gint                         num_jobs, i;

        g_debug ("Activating print-notifications plugin");

        print_notifications_plugin->priv->actual_jobs = NULL;

        error = NULL;
        res = gsd_print_notifications_manager_start (GSD_PRINT_NOTIFICATIONS_PLUGIN (plugin)->priv->manager, &error);
        if (! res) {
                g_warning ("Unable to start print-notifications manager: %s", error->message);
                g_error_free (error);
        }

        print_notifications_plugin->priv->cups_proxy =
          g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                         0,
                                         NULL,
                                         CUPS_DBUS_NAME,
                                         CUPS_DBUS_PATH,
                                         CUPS_DBUS_INTERFACE,
                                         NULL,
                                         &error);

        print_notifications_plugin->priv->bus_connection =
          g_dbus_proxy_get_connection (print_notifications_plugin->priv->cups_proxy);

        g_dbus_connection_signal_subscribe  (print_notifications_plugin->priv->bus_connection,
                                             NULL,
                                             CUPS_DBUS_INTERFACE,
                                             NULL,
                                             CUPS_DBUS_PATH,
                                             NULL,
                                             0,
                                             cups_notification_cb,
                                             print_notifications_plugin,
                                             NULL);

        num_jobs = cupsGetJobs (&jobs, NULL, 1, CUPS_WHICHJOBS_ACTIVE);

        for (i = 0; i < num_jobs; i++)
          print_notifications_plugin->priv->actual_jobs =
            g_slist_append (print_notifications_plugin->priv->actual_jobs, GUINT_TO_POINTER (jobs[i].id));

        cupsFreeJobs (num_jobs, jobs);
}

static void
impl_deactivate (GnomeSettingsPlugin *plugin)
{
        GsdPrintNotificationsPlugin *print_notifications_plugin = GSD_PRINT_NOTIFICATIONS_PLUGIN (plugin);

        g_debug ("Deactivating print_notifications plugin");
        gsd_print_notifications_manager_stop (GSD_PRINT_NOTIFICATIONS_PLUGIN (plugin)->priv->manager);

        g_slist_free (print_notifications_plugin->priv->actual_jobs);
        print_notifications_plugin->priv->actual_jobs = NULL;

        print_notifications_plugin->priv->bus_connection = NULL;

        if (print_notifications_plugin->priv->cups_proxy != NULL)
          g_object_unref (print_notifications_plugin->priv->cups_proxy);
}

static void
gsd_print_notifications_plugin_class_init (GsdPrintNotificationsPluginClass *klass)
{
        GObjectClass             *object_class = G_OBJECT_CLASS (klass);
        GnomeSettingsPluginClass *plugin_class = GNOME_SETTINGS_PLUGIN_CLASS (klass);

        object_class->finalize = gsd_print_notifications_plugin_finalize;

        plugin_class->activate = impl_activate;
        plugin_class->deactivate = impl_deactivate;

        g_type_class_add_private (klass, sizeof (GsdPrintNotificationsPluginPrivate));
}

static void
gsd_print_notifications_plugin_class_finalize (GsdPrintNotificationsPluginClass *klass)
{
}

