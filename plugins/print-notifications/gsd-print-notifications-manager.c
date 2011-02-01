/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Red Hat, Inc.
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

#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <cups/cups.h>
#include <libnotify/notify.h>
#include <libnotify/notify.h>

#include "gnome-settings-profile.h"
#include "gsd-print-notifications-manager.h"

#define GSD_PRINT_NOTIFICATIONS_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_PRINT_NOTIFICATIONS_MANAGER, GsdPrintNotificationsManagerPrivate))

#define CUPS_DBUS_NAME      "com.redhat.PrinterSpooler"
#define CUPS_DBUS_PATH      "/com/redhat/PrinterSpooler"
#define CUPS_DBUS_INTERFACE "com.redhat.PrinterSpooler"

struct GsdPrintNotificationsManagerPrivate
{
        GDBusProxy                   *cups_proxy;
        GDBusConnection              *bus_connection;
        GSList                       *actual_jobs;
};

enum {
        PROP_0,
};

static void     gsd_print_notifications_manager_class_init  (GsdPrintNotificationsManagerClass *klass);
static void     gsd_print_notifications_manager_init        (GsdPrintNotificationsManager      *print_notifications_manager);
static void     gsd_print_notifications_manager_finalize    (GObject                           *object);

G_DEFINE_TYPE (GsdPrintNotificationsManager, gsd_print_notifications_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static char *
get_dest_attr (const char *dest_name,
               const char *attr)
{
        cups_dest_t *dests;
        int          num_dests;
        cups_dest_t *dest;
        const char  *value;
        char        *ret;

        if (dest_name == NULL)
                return NULL;

        ret = NULL;

        num_dests = cupsGetDests (&dests);
        if (num_dests < 1) {
                g_debug ("Unable to get printer destinations");
                return NULL;
        }

        dest = cupsGetDest (dest_name, NULL, num_dests, dests);
        if (dest == NULL) {
                g_debug ("Unable to find a printer named '%s'", dest_name);
                goto out;
        }

        value = cupsGetOption (attr, dest->num_options, dest->options);
        if (value == NULL) {
                g_debug ("Unable to get %s for '%s'", attr, dest_name);
                goto out;
        }
        ret = g_strdup (value);
 out:
        cupsFreeDests (num_dests, dests);

        return ret;
}

static gboolean
is_local_dest (const char *name)
{
        char        *type_str;
        cups_ptype_t type;
        gboolean     is_remote;

        is_remote = TRUE;

        type_str = get_dest_attr (name, "printer-type");
        if (type_str == NULL) {
                goto out;
        }

        type = atoi (type_str);
        is_remote = type & (CUPS_PRINTER_REMOTE | CUPS_PRINTER_IMPLICIT);
        g_free (type_str);
 out:
        return !is_remote;
}

static void
on_cups_notification (GDBusConnection *connection,
                      const char      *sender_name,
                      const char      *object_path,
                      const char      *interface_name,
                      const char      *signal_name,
                      GVariant        *parameters,
                      gpointer         user_data)
{
        GsdPrintNotificationsManager *manager = GSD_PRINT_NOTIFICATIONS_MANAGER (user_data);
        cups_job_t                  *jobs;
        GSList                      *actual = NULL;
        GSList                      *tmp = NULL;
        gchar                       *printer_name = NULL;
        gchar                       *display_name = NULL;
        gchar                       *user_name = NULL;
        gchar                       *primary_text = NULL;
        gchar                       *secondary_text = NULL;
        guint                        job_id;
        gint                         actual_job = -1;
        gint                         num_jobs, i;
        gint                         index = -1;

        if (g_strcmp0 (signal_name, "PrinterAdded") == 0) {
                /* Translators: New printer has been added */
                if (g_variant_n_children (parameters) == 1) {
                        g_variant_get (parameters, "(&s)", &printer_name);
                        if (is_local_dest (printer_name)) {
                                primary_text = g_strdup (_("Printer added"));
                                secondary_text = get_dest_attr (printer_name, "printer-info");
                        }
                }
        } else if (g_strcmp0 (signal_name, "PrinterRemoved") == 0) {
                /* Translators: A printer has been removed */
                if (g_variant_n_children (parameters) == 1) {
                        g_variant_get (parameters, "(&s)", &printer_name);
                        if (is_local_dest (printer_name)) {
                                primary_text = g_strdup (_("Printer removed"));
                                secondary_text = get_dest_attr (printer_name, "printer-info");
                        }
                }
        } else if (g_strcmp0 (signal_name, "QueueChanged") == 0) {
                if (g_variant_n_children (parameters) == 1 ||
                    g_variant_n_children (parameters) == 3) {
                        g_variant_get (parameters, "(&s)", &printer_name);
                }

                display_name = get_dest_attr (printer_name, "printer-info");

                if (manager->priv->actual_jobs != NULL) {
                        num_jobs = cupsGetJobs (&jobs, printer_name, 1, CUPS_WHICHJOBS_ALL);

                        for (actual = manager->priv->actual_jobs; actual; actual = actual->next) {
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
                                                        primary_text = g_strdup (_("Printing stopped"));
                                                        /* Translators: "print-job xy" on a printer */
                                                        secondary_text = g_strdup_printf (_("\"%s\" on %s"), jobs[i].title, display_name);
                                                        actual->data = GINT_TO_POINTER (-1);
                                                        break;
                                                case IPP_JOB_CANCELED:
                                                        /* Translators: A print job has been canceled */
                                                        primary_text = g_strdup (_("Printing canceled"));
                                                        /* Translators: "print-job xy" on a printer */
                                                        secondary_text = g_strdup_printf (_("\"%s\" on %s"), jobs[i].title, display_name);
                                                        actual->data = GINT_TO_POINTER (-1);
                                                        break;
                                                case IPP_JOB_ABORTED:
                                                        /* Translators: A print job has been aborted */
                                                        primary_text = g_strdup (_("Printing aborted"));
                                                        /* Translators: "print-job xy" on a printer */
                                                        secondary_text = g_strdup_printf (_("\"%s\" on %s"), jobs[i].title, display_name);
                                                        actual->data = GINT_TO_POINTER (-1);
                                                        break;
                                                case IPP_JOB_COMPLETED:
                                                        /* Translators: A print job has been completed */
                                                        primary_text = g_strdup (_("Printing completed"));
                                                        /* Translators: "print-job xy" on a printer */
                                                        secondary_text = g_strdup_printf (_("\"%s\" on %s"), jobs[i].title, display_name);
                                                        actual->data = GINT_TO_POINTER (-1);
                                                        break;
                                                }
                                                break;
                                        }
                                }
                        }

                        for (actual = manager->priv->actual_jobs; actual; actual = actual->next) {
                                if (GPOINTER_TO_INT (actual->data) < 0) {
                                        tmp = actual->next;
                                        manager->priv->actual_jobs =
                                                g_slist_delete_link (manager->priv->actual_jobs, actual);
                                        actual = tmp;
                                        if (!actual)
                                                break;
                                }
                        }

                        cupsFreeJobs (num_jobs, jobs);
                }
        } else if (g_strcmp0 (signal_name, "JobQueuedLocal") == 0) {
                if (g_variant_n_children (parameters) == 3) {
                        g_variant_get (parameters, "(&su&s)", &printer_name, &job_id, &user_name);

                        num_jobs = cupsGetJobs (&jobs, printer_name, 1, CUPS_WHICHJOBS_ALL);

                        index = -1;
                        for (i = 0; i < num_jobs; i++)
                                if (jobs[i].id == job_id)
                                        index = i;

                        if (index >= 0) {
                                /* Only display a notification if there is some problem */
                        }

                        manager->priv->actual_jobs =
                                g_slist_append (manager->priv->actual_jobs, GUINT_TO_POINTER (job_id));

                        cupsFreeJobs (num_jobs, jobs);
                }
        } else if (g_strcmp0 (signal_name, "JobStartedLocal") == 0) {
                if (g_variant_n_children (parameters) == 3) {
                        g_variant_get (parameters, "(&su&s)", &printer_name, &job_id, &user_name);

                        display_name = get_dest_attr (printer_name, "printer-info");

                        for (actual = manager->priv->actual_jobs; actual; actual = actual->next) {
                                if (GPOINTER_TO_INT (actual->data) == job_id) {
                                        num_jobs = cupsGetJobs (&jobs, printer_name, 1, CUPS_WHICHJOBS_ALL);

                                        index = -1;
                                        for (i = 0; i < num_jobs; i++)
                                                if (jobs[i].id == job_id)
                                                        index = i;

                                        if (index >= 0) {
                                                /* Translators: A job is printing */
                                                primary_text = g_strdup (_("Printing"));
                                                /* Translators: "print-job xy" on a printer */
                                                secondary_text = g_strdup_printf (_("\"%s\" on %s"), jobs[index].title, display_name);
                                        }

                                        cupsFreeJobs (num_jobs, jobs);
                                }
                        }
                }
        }

        if (primary_text) {
                NotifyNotification *notification;
                notification = notify_notification_new (primary_text,
                                                        secondary_text,
                                                        "printer-symbolic");
                notify_notification_set_hint (notification, "transient", g_variant_new_boolean (TRUE));

                notify_notification_show (notification, NULL);
        }
}

gboolean
gsd_print_notifications_manager_start (GsdPrintNotificationsManager *manager,
                                       GError                      **error)
{
        cups_job_t *jobs;
        GError     *lerror;
        int         num_jobs;
        int         i;

        g_debug ("Starting print-notifications manager");

        gnome_settings_profile_start (NULL);

        manager->priv->actual_jobs = NULL;

        lerror = NULL;
        manager->priv->cups_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                                   0,
                                                                   NULL,
                                                                   CUPS_DBUS_NAME,
                                                                   CUPS_DBUS_PATH,
                                                                   CUPS_DBUS_INTERFACE,
                                                                   NULL,
                                                                   &lerror);
        if (lerror != NULL) {
                g_propagate_error (error, lerror);
                return FALSE;
        }

        manager->priv->bus_connection = g_dbus_proxy_get_connection (manager->priv->cups_proxy);

        g_dbus_connection_signal_subscribe (manager->priv->bus_connection,
                                            NULL,
                                            CUPS_DBUS_INTERFACE,
                                            NULL,
                                            CUPS_DBUS_PATH,
                                            NULL,
                                            0,
                                            on_cups_notification,
                                            manager,
                                            NULL);

        num_jobs = cupsGetJobs (&jobs, NULL, 1, CUPS_WHICHJOBS_ACTIVE);

        for (i = 0; i < num_jobs; i++) {
                manager->priv->actual_jobs = g_slist_append (manager->priv->actual_jobs, GUINT_TO_POINTER (jobs[i].id));
        }

        cupsFreeJobs (num_jobs, jobs);

        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_print_notifications_manager_stop (GsdPrintNotificationsManager *manager)
{
        g_debug ("Stopping print-notifications manager");

        if (manager->priv->actual_jobs != NULL) {
                g_slist_free (manager->priv->actual_jobs);
                manager->priv->actual_jobs = NULL;
        }

        manager->priv->bus_connection = NULL;

        if (manager->priv->cups_proxy != NULL) {
                g_object_unref (manager->priv->cups_proxy);
                manager->priv->cups_proxy = NULL;
        }
}

static void
gsd_print_notifications_manager_set_property (GObject        *object,
                                              guint           prop_id,
                                              const GValue   *value,
                                              GParamSpec     *pspec)
{
        GsdPrintNotificationsManager *self;

        self = GSD_PRINT_NOTIFICATIONS_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsd_print_notifications_manager_get_property (GObject        *object,
                                              guint           prop_id,
                                              GValue         *value,
                                              GParamSpec     *pspec)
{
        GsdPrintNotificationsManager *self;

        self = GSD_PRINT_NOTIFICATIONS_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gsd_print_notifications_manager_constructor (GType                  type,
                                             guint                  n_construct_properties,
                                             GObjectConstructParam *construct_properties)
{
        GsdPrintNotificationsManager      *print_notifications_manager;
        GsdPrintNotificationsManagerClass *klass;

        klass = GSD_PRINT_NOTIFICATIONS_MANAGER_CLASS (g_type_class_peek (GSD_TYPE_PRINT_NOTIFICATIONS_MANAGER));

        print_notifications_manager = GSD_PRINT_NOTIFICATIONS_MANAGER (G_OBJECT_CLASS (gsd_print_notifications_manager_parent_class)->constructor (type,
                                                                                                                                                   n_construct_properties,
                                                                                                                                                   construct_properties));

        return G_OBJECT (print_notifications_manager);
}

static void
gsd_print_notifications_manager_dispose (GObject *object)
{
        GsdPrintNotificationsManager *print_notifications_manager;

        print_notifications_manager = GSD_PRINT_NOTIFICATIONS_MANAGER (object);

        G_OBJECT_CLASS (gsd_print_notifications_manager_parent_class)->dispose (object);
}

static void
gsd_print_notifications_manager_class_init (GsdPrintNotificationsManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsd_print_notifications_manager_get_property;
        object_class->set_property = gsd_print_notifications_manager_set_property;
        object_class->constructor = gsd_print_notifications_manager_constructor;
        object_class->dispose = gsd_print_notifications_manager_dispose;
        object_class->finalize = gsd_print_notifications_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdPrintNotificationsManagerPrivate));
}

static void
gsd_print_notifications_manager_init (GsdPrintNotificationsManager *manager)
{
        manager->priv = GSD_PRINT_NOTIFICATIONS_MANAGER_GET_PRIVATE (manager);

}

static void
gsd_print_notifications_manager_finalize (GObject *object)
{
        GsdPrintNotificationsManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_PRINT_NOTIFICATIONS_MANAGER (object));

        manager = GSD_PRINT_NOTIFICATIONS_MANAGER (object);

        g_return_if_fail (manager->priv != NULL);

        if (manager->priv->cups_proxy != NULL) {
                g_object_unref (manager->priv->cups_proxy);
        }

        g_slist_free (manager->priv->actual_jobs);

        G_OBJECT_CLASS (gsd_print_notifications_manager_parent_class)->finalize (object);
}

GsdPrintNotificationsManager *
gsd_print_notifications_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_PRINT_NOTIFICATIONS_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_PRINT_NOTIFICATIONS_MANAGER (manager_object);
}
