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

#define CUPS_DBUS_NAME      "org.cups.cupsd.Notifier"
#define CUPS_DBUS_PATH      "/org/cups/cupsd/Notifier"
#define CUPS_DBUS_INTERFACE "org.cups.cupsd.Notifier"

struct GsdPrintNotificationsManagerPrivate
{
        GDBusProxy                   *cups_proxy;
        GDBusConnection              *bus_connection;
        gint                          subscription_id;
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
        gboolean                     printer_is_accepting_jobs;
        gboolean                     my_job = FALSE;
        http_t                      *http;
        gchar                       *printer_name = NULL;
        gchar                       *display_name = NULL;
        gchar                       *primary_text = NULL;
        gchar                       *secondary_text = NULL;
        gchar                       *text = NULL;
        gchar                       *printer_uri = NULL;
        gchar                       *printer_state_reasons = NULL;
        gchar                       *job_state_reasons = NULL;
        gchar                       *job_name = NULL;
        gchar                       *job_uri = NULL;
        guint                        job_id;
        ipp_t                       *request, *response;
        gint                         printer_state;
        gint                         job_state;
        gint                         job_impressions_completed;

        if (g_strcmp0 (signal_name, "PrinterAdded") != 0 &&
            g_strcmp0 (signal_name, "PrinterDeleted") != 0 &&
            g_strcmp0 (signal_name, "JobCompleted") != 0 &&
            g_strcmp0 (signal_name, "JobState") != 0 &&
            g_strcmp0 (signal_name, "JobCreated") != 0)
                return;

        if (g_variant_n_children (parameters) == 1) {
                g_variant_get (parameters, "(&s)", &text);
        } else if (g_variant_n_children (parameters) == 6) {
                g_variant_get (parameters, "(&s&s&su&sb)",
                               &text,
                               &printer_uri,
                               &printer_name,
                               &printer_state,
                               &printer_state_reasons,
                               &printer_is_accepting_jobs);
        } else if (g_variant_n_children (parameters) == 11) {
                ipp_attribute_t *attr;

                g_variant_get (parameters, "(&s&s&su&sbuu&s&su)",
                               &text,
                               &printer_uri,
                               &printer_name,
                               &printer_state,
                               &printer_state_reasons,
                               &printer_is_accepting_jobs,
                               &job_id,
                               &job_state,
                               &job_state_reasons,
                               &job_name,
                               &job_impressions_completed);

                if ((http = httpConnectEncrypt (cupsServer (), ippPort (),
                                                cupsEncryption ())) == NULL) {
                        g_debug ("Connection to CUPS server \'%s\' failed.", cupsServer ());
                }
                else {
                        job_uri = g_strdup_printf ("ipp://localhost/jobs/%d", job_id);

                        request = ippNewRequest(IPP_GET_JOB_ATTRIBUTES);
                        ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "job-uri", NULL, job_uri);
                        ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                                     "requested-attributes", NULL, "job-originating-user-name");
                        response = cupsDoRequest(http, request, "/");

                        if (response) {
                                if (response->request.status.status_code <= IPP_OK_CONFLICT &&
                                    (attr = ippFindAttribute(response, "job-originating-user-name",
                                                             IPP_TAG_NAME))) {
                                        if (g_strcmp0 (attr->values[0].string.text, cupsUser ()) == 0)
                                                my_job = TRUE;
                                }
                                ippDelete(response);
                        }
                        g_free (job_uri);
                }
        }

        if (g_strcmp0 (signal_name, "PrinterAdded") == 0) {
                /* Translators: New printer has been added */
                if (is_local_dest (printer_name)) {
                        primary_text = g_strdup (_("Printer added"));
                        secondary_text = g_strdup (printer_name);
                }
        } else if (g_strcmp0 (signal_name, "PrinterDeleted") == 0) {
                /* Translators: A printer has been removed */
                if (is_local_dest (printer_name)) {
                        primary_text = g_strdup (_("Printer removed"));
                        secondary_text = g_strdup (printer_name);
                }
        } else if (g_strcmp0 (signal_name, "JobCompleted") == 0 && my_job) {
                /* FIXME: get a better human readable name */
                display_name = g_strdup (printer_name);

                switch (job_state) {
                        case IPP_JOB_PENDING:
                        case IPP_JOB_HELD:
                        case IPP_JOB_PROCESSING:
                                break;
                        case IPP_JOB_STOPPED:
                                /* Translators: A print job has been stopped */
                                primary_text = g_strdup (_("Printing stopped"));
                                /* Translators: "print-job xy" on a printer */
                                secondary_text = g_strdup_printf (_("\"%s\" on %s"), job_name, display_name);
                                break;
                        case IPP_JOB_CANCELED:
                                /* Translators: A print job has been canceled */
                                primary_text = g_strdup (_("Printing canceled"));
                                /* Translators: "print-job xy" on a printer */
                                secondary_text = g_strdup_printf (_("\"%s\" on %s"), job_name, display_name);
                                break;
                        case IPP_JOB_ABORTED:
                                /* Translators: A print job has been aborted */
                                primary_text = g_strdup (_("Printing aborted"));
                                /* Translators: "print-job xy" on a printer */
                                secondary_text = g_strdup_printf (_("\"%s\" on %s"), job_name, display_name);
                                break;
                        case IPP_JOB_COMPLETED:
                                /* Translators: A print job has been completed */
                                primary_text = g_strdup (_("Printing completed"));
                                /* Translators: "print-job xy" on a printer */
                                secondary_text = g_strdup_printf (_("\"%s\" on %s"), job_name, display_name);
                                break;
                }
        } else if (g_strcmp0 (signal_name, "JobState") == 0 && my_job) {
                display_name = get_dest_attr (printer_name, "printer-info");

                switch (job_state) {
                        case IPP_JOB_PROCESSING:
                                /* Translators: A job is printing */
                                primary_text = g_strdup (_("Printing"));
                                /* Translators: "print-job xy" on a printer */
                                secondary_text = g_strdup_printf (_("\"%s\" on %s"), job_name, display_name);
                                break;
                        default:
                                break;
                }
        } else if (g_strcmp0 (signal_name, "JobCreated") == 0 && my_job) {
                display_name = get_dest_attr (printer_name, "printer-info");

                if (job_state == IPP_JOB_PROCESSING) {
                        /* Translators: A job is printing */
                        primary_text = g_strdup (_("Printing"));
                        /* Translators: "print-job xy" on a printer */
                        secondary_text = g_strdup_printf (_("\"%s\" on %s"), job_name, display_name);
                }
        }

        g_free (display_name);


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
        GError     *lerror;
        ipp_t      *request, *response;
        http_t     *http;
        gint        num_events = 6;
        static const char * const events[] = {
                "job-created",
                "job-completed",
                "job-state-changed",
                "job-state",
                "printer-added",
                "printer-deleted"};
        ipp_attribute_t *attr = NULL;

        g_debug ("Starting print-notifications manager");

        gnome_settings_profile_start (NULL);

        manager->priv->subscription_id = -1;

        if ((http = httpConnectEncrypt (cupsServer (), ippPort (),
                                        cupsEncryption ())) == NULL) {
                g_debug ("Connection to CUPS server \'%s\' failed.", cupsServer ());
        }
        else {
                request = ippNewRequest(IPP_CREATE_PRINTER_SUBSCRIPTION);
                ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL,
                             "/");
                ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name",
                             NULL, cupsUser ());
                ippAddStrings(request, IPP_TAG_SUBSCRIPTION, IPP_TAG_KEYWORD, "notify-events",
                              num_events, NULL, events);
                ippAddString(request, IPP_TAG_SUBSCRIPTION, IPP_TAG_KEYWORD,
                             "notify-pull-method", NULL, "ippget");
                ippAddString(request, IPP_TAG_SUBSCRIPTION, IPP_TAG_URI, "notify-recipient-uri",
                             NULL, "dbus://");
                ippAddInteger(request, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER,
                              "notify-lease-duration", 0);
                response = cupsDoRequest(http, request, "/");

                if (response != NULL && response->request.status.status_code <= IPP_OK_CONFLICT) {
                        if ((attr = ippFindAttribute(response, "notify-subscription-id",
                                                     IPP_TAG_INTEGER)) == NULL)
                                g_debug ("No notify-subscription-id in response!\n");
                        else
                                manager->priv->subscription_id = attr->values[0].integer;
                }

                if (response)
                  ippDelete(response);

                httpClose(http);
        }

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

        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_print_notifications_manager_stop (GsdPrintNotificationsManager *manager)
{
        ipp_t      *request;
        http_t     *http;

        g_debug ("Stopping print-notifications manager");

        if (manager->priv->subscription_id >= 0 &&
            ((http = httpConnectEncrypt(cupsServer(), ippPort(),
                                       cupsEncryption())) != NULL)) {
                request = ippNewRequest(IPP_CANCEL_SUBSCRIPTION);
                ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL,
                             "/");
                ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name",
                             NULL, cupsUser ());
                ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER,
                              "notify-subscription-ids", manager->priv->subscription_id);
                ippDelete(cupsDoRequest(http, request, "/"));
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

        print_notifications_manager = GSD_PRINT_NOTIFICATIONS_MANAGER (G_OBJECT_CLASS (gsd_print_notifications_manager_parent_class)->constructor (type,
                                                                                                                                                   n_construct_properties,
                                                                                                                                                   construct_properties));

        return G_OBJECT (print_notifications_manager);
}

static void
gsd_print_notifications_manager_dispose (GObject *object)
{
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
