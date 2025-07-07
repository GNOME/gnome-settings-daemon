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
#include <colord.h>
#include <libnotify/notify.h>

#include "gsd-color-calibrate.h"

#define GCM_SESSION_NOTIFY_TIMEOUT                      30000 /* ms */
#define GCM_SETTINGS_RECALIBRATE_PRINTER_THRESHOLD      "recalibrate-printer-threshold"
#define GCM_SETTINGS_RECALIBRATE_DISPLAY_THRESHOLD      "recalibrate-display-threshold"

struct _GsdColorCalibrate
{
        GObject          parent;

        GsdColorManager *manager;
        CdClient        *client;
        GSettings       *settings;
};

enum {
        PROP_0,
        PROP_MANAGER,
        N_PROPS,
};

static GParamSpec *props[N_PROPS] = { 0, };

static void     gsd_color_calibrate_class_init  (GsdColorCalibrateClass *klass);
static void     gsd_color_calibrate_init        (GsdColorCalibrate      *color_calibrate);
static void     gsd_color_calibrate_finalize    (GObject             *object);

G_DEFINE_TYPE (GsdColorCalibrate, gsd_color_calibrate, G_TYPE_OBJECT)

typedef struct {
        GsdColorCalibrate       *calibrate;
        CdProfile               *profile;
        CdDevice                *device;
        guint32                  output_id;
} GcmSessionAsyncHelper;

static void
gcm_session_async_helper_free (GcmSessionAsyncHelper *helper)
{
        if (helper->calibrate != NULL)
                g_object_unref (helper->calibrate);
        if (helper->profile != NULL)
                g_object_unref (helper->profile);
        if (helper->device != NULL)
                g_object_unref (helper->device);
        g_free (helper);
}

static void
gcm_session_exec_control_center (GsdColorCalibrate *calibrate)
{
        g_autoptr (GError) error = NULL;
        g_autoptr (GAppInfo) app_info = NULL;
        g_autoptr (GAppLaunchContext) launch_context = NULL;

        /* setup the launch context so the startup notification is correct */
        launch_context = g_app_launch_context_new ();
        app_info = g_app_info_create_from_commandline (BINDIR "/gnome-control-center color",
                                                       "gnome-control-center",
                                                       G_APP_INFO_CREATE_SUPPORTS_STARTUP_NOTIFICATION,
                                                       &error);
        if (app_info == NULL) {
                g_warning ("failed to create application info: %s",
                           error->message);\
                return;
        }

        /* launch gnome-control-center */
        if (!g_app_info_launch (app_info,
                                NULL,
                                G_APP_LAUNCH_CONTEXT (launch_context),
                                &error)) {
                g_warning ("failed to launch gnome-control-center: %s",
                           error->message);
        }
}

static void
gcm_session_notify_cb (NotifyNotification *notification,
                       gchar *action,
                       gpointer user_data)
{
        GsdColorCalibrate *calibrate = GSD_COLOR_CALIBRATE (user_data);

        if (g_strcmp0 (action, "recalibrate") == 0) {
                notify_notification_close (notification, NULL);
                gcm_session_exec_control_center (calibrate);
        }
}

static void
closed_cb (NotifyNotification *notification, gpointer data)
{
        g_object_unref (notification);
}

static gboolean
gcm_session_notify_recalibrate (GsdColorCalibrate *calibrate,
                                const gchar *title,
                                const gchar *message,
                                CdDeviceKind kind)
{
        gboolean ret;
        GError *error = NULL;
        NotifyNotification *notification;

        /* show a bubble */
        notification = notify_notification_new (title, message, NULL);
        notify_notification_set_timeout (notification, GCM_SESSION_NOTIFY_TIMEOUT);
        notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
        notify_notification_set_app_name (notification, _("Color"));
        notify_notification_set_hint_string (notification, "desktop-entry", "gnome-color-panel");

        notify_notification_add_action (notification,
                                        "recalibrate",
                                        /* TRANSLATORS: button: this is to open GCM */
                                        _("Recalibrate now"),
                                        gcm_session_notify_cb,
                                        calibrate, NULL);

        g_signal_connect (notification, "closed", G_CALLBACK (closed_cb), NULL);
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
gcm_session_notify_device (GsdColorCalibrate *calibrate, CdDevice *device)
{
        CdDeviceKind kind;
        const gchar *title;
        gchar *device_title = NULL;
        gchar *message;
        guint threshold;
        glong since;

        /* TRANSLATORS: this is when the device has not been recalibrated in a while */
        title = _("Recalibration required");
        device_title = gcm_session_device_get_title (device);

        /* check we care */
        kind = cd_device_get_kind (device);
        if (kind == CD_DEVICE_KIND_DISPLAY) {

                /* get from GSettings */
                threshold = g_settings_get_uint (calibrate->settings,
                                                 GCM_SETTINGS_RECALIBRATE_DISPLAY_THRESHOLD);

                /* TRANSLATORS: this is when the display has not been recalibrated in a while */
                message = g_strdup_printf (_("The display “%s” should be recalibrated soon."),
                                           device_title);
        } else {

                /* get from GSettings */
                threshold = g_settings_get_uint (calibrate->settings,
                                                 GCM_SETTINGS_RECALIBRATE_PRINTER_THRESHOLD);

                /* TRANSLATORS: this is when the printer has not been recalibrated in a while */
                message = g_strdup_printf (_("The printer “%s” should be recalibrated soon."),
                                           device_title);
        }

        /* check if we need to notify */
        since = (g_get_real_time () - cd_device_get_modified (device)) / G_USEC_PER_SEC;
        if (threshold > since)
                gcm_session_notify_recalibrate (calibrate, title, message, kind);
        g_free (device_title);
        g_free (message);
}

static void
gcm_session_profile_connect_cb (GObject *object,
                                GAsyncResult *res,
                                gpointer user_data)
{
        const gchar *filename;
        gboolean ret;
        gchar *basename = NULL;
        const gchar *data_source;
        GError *error = NULL;
        CdProfile *profile = CD_PROFILE (object);
        GcmSessionAsyncHelper *helper = (GcmSessionAsyncHelper *) user_data;
        GsdColorCalibrate *calibrate = GSD_COLOR_CALIBRATE (helper->calibrate);

        ret = cd_profile_connect_finish (profile,
                                         res,
                                         &error);
        if (!ret) {
                g_warning ("failed to connect to profile: %s",
                           error->message);
                g_error_free (error);
                goto out;
        }

        /* ensure it's a profile generated by us */
        data_source = cd_profile_get_metadata_item (profile,
                                                    CD_PROFILE_METADATA_DATA_SOURCE);
        if (data_source == NULL) {

                /* existing profiles from gnome-color-calibrate < 3.1
                 * won't have the extra metadata values added */
                filename = cd_profile_get_filename (profile);
                if (filename == NULL)
                        goto out;
                basename = g_path_get_basename (filename);
                if (!g_str_has_prefix (basename, "GCM")) {
                        g_debug ("not a GCM profile for %s: %s",
                                 cd_device_get_id (helper->device), filename);
                        goto out;
                }

        /* ensure it's been created from a calibration, rather than from
         * auto-EDID */
        } else if (g_strcmp0 (data_source,
                   CD_PROFILE_METADATA_DATA_SOURCE_CALIB) != 0) {
                g_debug ("not a calib profile for %s",
                         cd_device_get_id (helper->device));
                goto out;
        }

        /* handle device */
        gcm_session_notify_device (calibrate, helper->device);
out:
        gcm_session_async_helper_free (helper);
        g_free (basename);
}

static void
gcm_session_device_connect_cb (GObject *object,
                               GAsyncResult *res,
                               gpointer user_data)
{
        gboolean ret;
        GError *error = NULL;
        CdDeviceKind kind;
        CdProfile *profile = NULL;
        CdDevice *device = CD_DEVICE (object);
        GsdColorCalibrate *calibrate = GSD_COLOR_CALIBRATE (user_data);
        GcmSessionAsyncHelper *helper;

        ret = cd_device_connect_finish (device,
                                        res,
                                        &error);
        if (!ret) {
                g_warning ("failed to connect to device: %s",
                           error->message);
                g_error_free (error);
                goto out;
        }

        /* check we care */
        kind = cd_device_get_kind (device);
        if (kind != CD_DEVICE_KIND_DISPLAY &&
            kind != CD_DEVICE_KIND_PRINTER)
                goto out;

        /* ensure we have a profile */
        profile = cd_device_get_default_profile (device);
        if (profile == NULL) {
                g_debug ("no profile set for %s", cd_device_get_id (device));
                goto out;
        }

        /* connect to the profile */
        helper = g_new0 (GcmSessionAsyncHelper, 1);
        helper->calibrate = g_object_ref (calibrate);
        helper->device = g_object_ref (device);
        cd_profile_connect (profile,
                            NULL,
                            gcm_session_profile_connect_cb,
                            helper);
out:
        if (profile != NULL)
                g_object_unref (profile);
}

static void
gcm_session_device_added_notify_cb (CdClient *client,
                                    CdDevice *device,
                                    GsdColorCalibrate *calibrate)
{
        /* connect to the device to get properties */
        cd_device_connect (device,
                           NULL,
                           gcm_session_device_connect_cb,
                           calibrate);
}

static void
gcm_session_sensor_added_cb (CdClient *client,
                             CdSensor *sensor,
                             GsdColorCalibrate *calibrate)
{
        GsdApplication *gsd_app = GSD_APPLICATION (calibrate->manager);
        ca_context *ca_context = gsd_application_get_ca_context (gsd_app);

        ca_context_play (ca_context, 0,
                         CA_PROP_EVENT_ID, "device-added",
                         /* TRANSLATORS: this is the application name */
                         CA_PROP_APPLICATION_NAME, _("GNOME Settings Daemon Color Plugin"),
                        /* TRANSLATORS: this is a sound description */
                         CA_PROP_EVENT_DESCRIPTION, _("Color calibration device added"), NULL);

        /* open up the color prefs window */
        gcm_session_exec_control_center (calibrate);
}

static void
gcm_session_sensor_removed_cb (CdClient *client,
                               CdSensor *sensor,
                               GsdColorCalibrate *calibrate)
{
        GsdApplication *gsd_app = GSD_APPLICATION (calibrate->manager);
        ca_context *ca_context = gsd_application_get_ca_context (gsd_app);

        ca_context_play (ca_context, 0,
                         CA_PROP_EVENT_ID, "device-removed",
                         /* TRANSLATORS: this is the application name */
                         CA_PROP_APPLICATION_NAME, _("GNOME Settings Daemon Color Plugin"),
                        /* TRANSLATORS: this is a sound description */
                         CA_PROP_EVENT_DESCRIPTION, _("Color calibration device removed"), NULL);
}

static void
gsd_color_calibrate_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
        GsdColorCalibrate *calibrate = GSD_COLOR_CALIBRATE (object);

        switch (prop_id) {
        case PROP_MANAGER:
                calibrate->manager = g_value_get_object (value);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsd_color_calibrate_class_init (GsdColorCalibrateClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->set_property = gsd_color_calibrate_set_property;
        object_class->finalize = gsd_color_calibrate_finalize;

        props[PROP_MANAGER] =
                g_param_spec_object ("manager", NULL, NULL,
                                     GSD_TYPE_COLOR_MANAGER,
                                     G_PARAM_WRITABLE |
                                     G_PARAM_CONSTRUCT_ONLY |
                                     G_PARAM_STATIC_STRINGS);

        g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
gsd_color_calibrate_init (GsdColorCalibrate *calibrate)
{
        calibrate->settings = g_settings_new ("org.gnome.settings-daemon.plugins.color");
        calibrate->client = cd_client_new ();
        g_signal_connect (calibrate->client, "device-added",
                          G_CALLBACK (gcm_session_device_added_notify_cb),
                          calibrate);
        g_signal_connect (calibrate->client, "sensor-added",
                          G_CALLBACK (gcm_session_sensor_added_cb),
                          calibrate);
        g_signal_connect (calibrate->client, "sensor-removed",
                          G_CALLBACK (gcm_session_sensor_removed_cb),
                          calibrate);
}

static void
gsd_color_calibrate_finalize (GObject *object)
{
        GsdColorCalibrate *calibrate;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_COLOR_CALIBRATE (object));

        calibrate = GSD_COLOR_CALIBRATE (object);

        g_clear_object (&calibrate->settings);
        g_clear_object (&calibrate->client);

        G_OBJECT_CLASS (gsd_color_calibrate_parent_class)->finalize (object);
}

GsdColorCalibrate *
gsd_color_calibrate_new (GsdColorManager *color_manager)
{
        return g_object_new (GSD_TYPE_COLOR_CALIBRATE,
                             "manager", color_manager,
                             NULL);
}
