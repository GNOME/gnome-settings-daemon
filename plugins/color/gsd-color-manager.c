/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2011 Richard Hughes <richard@hughsie.com>
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

#include <glib/gi18n.h>
#include <colord.h>
#include <libnotify/notify.h>

#ifdef HAVE_LIBCANBERRA
#include <canberra-gtk.h>
#endif

#include "gnome-settings-profile.h"
#include "gsd-color-manager.h"

#define GSD_COLOR_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_COLOR_MANAGER, GsdColorManagerPrivate))

#define GCM_SESSION_NOTIFY_TIMEOUT                      30000 /* ms */
#define GCM_SETTINGS_SHOW_NOTIFICATIONS                 "show-notifications"
#define GCM_SETTINGS_RECALIBRATE_PRINTER_THRESHOLD      "recalibrate-printer-threshold"
#define GCM_SETTINGS_RECALIBRATE_DISPLAY_THRESHOLD      "recalibrate-display-threshold"

struct GsdColorManagerPrivate
{
        CdClient        *client;
        GSettings       *settings;
};

enum {
        PROP_0,
};

static void     gsd_color_manager_class_init  (GsdColorManagerClass *klass);
static void     gsd_color_manager_init        (GsdColorManager      *color_manager);
static void     gsd_color_manager_finalize    (GObject             *object);

G_DEFINE_TYPE (GsdColorManager, gsd_color_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static void
gcm_session_client_connect_cb (GObject *source_object,
                               GAsyncResult *res,
                               gpointer user_data)
{
        gboolean ret;
        GError *error = NULL;
        GsdColorManager *manager = GSD_COLOR_MANAGER (user_data);

        /* connected */
        g_debug ("connected to colord");
        ret = cd_client_connect_finish (manager->priv->client, res, &error);
        if (!ret) {
                g_warning ("failed to connect to colord: %s", error->message);
                g_error_free (error);
        }
}

gboolean
gsd_color_manager_start (GsdColorManager *manager,
                         GError          **error)
{
        GsdColorManagerPrivate *priv = manager->priv;

        g_debug ("Starting color manager");
        gnome_settings_profile_start (NULL);

        cd_client_connect (priv->client,
                           NULL,
                           gcm_session_client_connect_cb,
                           manager);

        gnome_settings_profile_end (NULL);
        return TRUE;
}

void
gsd_color_manager_stop (GsdColorManager *manager)
{
        g_debug ("Stopping color manager");
}

static void
gcm_session_notify_cb (NotifyNotification *notification,
                       gchar *action,
                       gpointer user_data)
{
        gboolean ret;
        GError *error = NULL;
        GsdColorManager *manager = GSD_COLOR_MANAGER (user_data);
        GsdColorManagerPrivate *priv = manager->priv;

        if (g_strcmp0 (action, "display") == 0) {
                g_settings_set_uint (priv->settings,
                                     GCM_SETTINGS_RECALIBRATE_DISPLAY_THRESHOLD,
                                     0);
        } else if (g_strcmp0 (action, "printer") == 0) {
                g_settings_set_uint (priv->settings,
                                     GCM_SETTINGS_RECALIBRATE_PRINTER_THRESHOLD,
                                     0);
        } else if (g_strcmp0 (action, "recalibrate") == 0) {
                ret = g_spawn_command_line_async (BINDIR "/gnome-control-center color",
                                                  &error);
                if (!ret) {
                        g_warning ("failed to spawn: %s", error->message);
                        g_error_free (error);
                }
        }
}

static gboolean
gcm_session_notify_recalibrate (GsdColorManager *manager,
                                const gchar *title,
                                const gchar *message,
                                CdDeviceKind kind)
{
        gboolean ret;
        GError *error = NULL;
        NotifyNotification *notification;
        GsdColorManagerPrivate *priv = manager->priv;

        /* show a bubble */
        notification = notify_notification_new (title, message, "preferences-color");
        notify_notification_set_timeout (notification, GCM_SESSION_NOTIFY_TIMEOUT);
        notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);

        /* TRANSLATORS: button: this is to open GCM */
        notify_notification_add_action (notification,
                                        "recalibrate",
                                        _("Recalibrate now"),
                                        gcm_session_notify_cb,
                                        priv, NULL);

        /* TRANSLATORS: button: this is to ignore the recalibrate notifications */
        notify_notification_add_action (notification,
                                        cd_device_kind_to_string (kind),
                                        _("Ignore"),
                                        gcm_session_notify_cb,
                                        priv, NULL);

        ret = notify_notification_show (notification, &error);
        if (!ret) {
                g_warning ("failed to show notification: %s",
                           error->message);
                g_error_free (error);
        }
        return ret;
}

static gchar *
gcm_session_device_get_title (CdDevice *device)
{
        const gchar *vendor;
        const gchar *model;

        model = cd_device_get_model (device);
        vendor = cd_device_get_vendor (device);
        if (model != NULL && vendor != NULL)
                return g_strdup_printf ("%s - %s", vendor, model);
        if (vendor != NULL)
                return g_strdup (vendor);
        if (model != NULL)
                return g_strdup (model);
        return g_strdup (cd_device_get_id (device));
}

static void
gcm_session_notify_device (GsdColorManager *manager, CdDevice *device)
{
        CdDeviceKind kind;
        const gchar *title;
        gchar *device_title = NULL;
        gchar *message;
        gint threshold;
        glong since;
        GsdColorManagerPrivate *priv = manager->priv;

        /* TRANSLATORS: this is when the device has not been recalibrated in a while */
        title = _("Recalibration required");
        device_title = gcm_session_device_get_title (device);

        /* check we care */
        kind = cd_device_get_kind (device);
        if (kind == CD_DEVICE_KIND_DISPLAY) {

                /* get from GSettings */
                threshold = g_settings_get_int (priv->settings,
                                                GCM_SETTINGS_RECALIBRATE_DISPLAY_THRESHOLD);

                /* TRANSLATORS: this is when the display has not been recalibrated in a while */
                message = g_strdup_printf (_("The display '%s' should be recalibrated soon."),
                                           device_title);
        } else {

                /* get from GSettings */
                threshold = g_settings_get_int (priv->settings,
                                                GCM_SETTINGS_RECALIBRATE_PRINTER_THRESHOLD);

                /* TRANSLATORS: this is when the printer has not been recalibrated in a while */
                message = g_strdup_printf (_("The printer '%s' should be recalibrated soon."),
                                           device_title);
        }

        /* check if we need to notify */
        since = (g_get_real_time () / G_USEC_PER_SEC) - cd_device_get_modified (device);
        if (threshold > since)
                gcm_session_notify_recalibrate (manager, title, message, kind);
        g_free (device_title);
        g_free (message);
}

static void
gcm_session_device_added_notify_cb (CdClient *client,
                                    CdDevice *device,
                                    GsdColorManager *manager)
{
        CdDeviceKind kind;
        CdProfile *profile;
        const gchar *filename;
        gchar *basename = NULL;
        gboolean allow_notifications;
        GsdColorManagerPrivate *priv = manager->priv;

        /* check we care */
        kind = cd_device_get_kind (device);
        if (kind != CD_DEVICE_KIND_DISPLAY &&
            kind != CD_DEVICE_KIND_PRINTER)
                return;

        /* ensure we have a profile */
        profile = cd_device_get_default_profile (device);
        if (profile == NULL) {
                g_debug ("no profile set for %s", cd_device_get_id (device));
                goto out;
        }

        /* ensure it's a profile generated by us */
        filename = cd_profile_get_filename (profile);
        basename = g_path_get_basename (filename);
        if (!g_str_has_prefix (basename, "GCM")) {
                g_debug ("not a GCM profile for %s: %s",
                         cd_device_get_id (device), filename);
                goto out;
        }

        /* do we allow notifications */
        allow_notifications = g_settings_get_boolean (priv->settings,
                                                      GCM_SETTINGS_SHOW_NOTIFICATIONS);
        if (!allow_notifications)
                goto out;

        /* handle device */
        gcm_session_notify_device (manager, device);
out:
        g_free (basename);
}


static void
gcm_session_sensor_added_cb (CdClient *client,
                             CdSensor *sensor,
                             GsdColorManager *manager)
{
        gboolean ret;
        GError *error = NULL;

#ifdef HAVE_LIBCANBERRA
        ca_context_play (ca_gtk_context_get (), 0,
                         CA_PROP_EVENT_ID, "device-added",
                         /* TRANSLATORS: this is the application name */
                         CA_PROP_APPLICATION_NAME, _("GNOME Settings Daemon Color Plugin"),
                        /* TRANSLATORS: this is a sound description */
                         CA_PROP_EVENT_DESCRIPTION, _("Color calibration device added"), NULL);
#endif

        /* open up the color prefs window */
        ret = g_spawn_command_line_async (BINDIR "/gnome-control-center color",
                                          &error);
        if (!ret) {
                g_warning ("failed to spawn: %s", error->message);
                g_error_free (error);
        }
}

static void
gcm_session_sensor_removed_cb (CdClient *client,
                               CdSensor *sensor,
                               GsdColorManager *manager)
{
#ifdef HAVE_LIBCANBERRA
        ca_context_play (ca_gtk_context_get (), 0,
                         CA_PROP_EVENT_ID, "device-removed",
                         /* TRANSLATORS: this is the application name */
                         CA_PROP_APPLICATION_NAME, _("GNOME Settings Daemon Color Plugin"),
                        /* TRANSLATORS: this is a sound description */
                         CA_PROP_EVENT_DESCRIPTION, _("Color calibration device removed"), NULL);
#endif
}

static void
gsd_color_manager_set_property (GObject        *object,
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
gsd_color_manager_get_property (GObject        *object,
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

static void
gsd_color_manager_dispose (GObject *object)
{
        G_OBJECT_CLASS (gsd_color_manager_parent_class)->dispose (object);
}

static void
gsd_color_manager_class_init (GsdColorManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsd_color_manager_get_property;
        object_class->set_property = gsd_color_manager_set_property;
        object_class->dispose = gsd_color_manager_dispose;
        object_class->finalize = gsd_color_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdColorManagerPrivate));
}

static void
gsd_color_manager_init (GsdColorManager *manager)
{
        GsdColorManagerPrivate *priv;
        priv = manager->priv = GSD_COLOR_MANAGER_GET_PRIVATE (manager);

        priv->settings = g_settings_new ("org.gnome.settings-daemon.plugins.color");
        priv->client = cd_client_new ();
        g_signal_connect (priv->client, "device-added",
                          G_CALLBACK (gcm_session_device_added_notify_cb),
                          manager);
        g_signal_connect (priv->client, "sensor-added",
                          G_CALLBACK (gcm_session_sensor_added_cb),
                          manager);
        g_signal_connect (priv->client, "sensor-removed",
                          G_CALLBACK (gcm_session_sensor_removed_cb),
                          manager);
}

static void
gsd_color_manager_finalize (GObject *object)
{
        GsdColorManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_COLOR_MANAGER (object));

        manager = GSD_COLOR_MANAGER (object);

        g_return_if_fail (manager->priv != NULL);

        g_object_unref (manager->priv->settings);
        g_object_unref (manager->priv->client);

        G_OBJECT_CLASS (gsd_color_manager_parent_class)->finalize (object);
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
        }

        return GSD_COLOR_MANAGER (manager_object);
}
