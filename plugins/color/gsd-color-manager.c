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

#ifdef HAVE_LCMS
  #include <lcms2.h>
#endif

#include "gnome-settings-profile.h"
#include "gsd-color-manager.h"
#include "gcm-profile-store.h"

#define GSD_COLOR_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_COLOR_MANAGER, GsdColorManagerPrivate))

#define GCM_SESSION_NOTIFY_TIMEOUT                      30000 /* ms */
#define GCM_SETTINGS_SHOW_NOTIFICATIONS                 "show-notifications"
#define GCM_SETTINGS_RECALIBRATE_PRINTER_THRESHOLD      "recalibrate-printer-threshold"
#define GCM_SETTINGS_RECALIBRATE_DISPLAY_THRESHOLD      "recalibrate-display-threshold"

struct GsdColorManagerPrivate
{
        CdClient        *client;
        GSettings       *settings;
        GcmProfileStore *profile_store;
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
        GsdColorManagerPrivate *priv = manager->priv;

        /* connected */
        g_debug ("connected to colord");
        ret = cd_client_connect_finish (manager->priv->client, res, &error);
        if (!ret) {
                g_warning ("failed to connect to colord: %s", error->message);
                g_error_free (error);
                return;
        }

        /* add profiles */
        gcm_profile_store_search (priv->profile_store);
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
gcm_session_exec_control_center (GsdColorManager *manager)
{
        gboolean ret;
        GError *error = NULL;
        GAppInfo *app_info;
        GdkAppLaunchContext *launch_context;

        /* setup the launch context so the startup notification is correct */
        launch_context = gdk_app_launch_context_new ();
        app_info = g_app_info_create_from_commandline (BINDIR "/gnome-control-center color",
                                                       "gnome-control-center",
                                                       G_APP_INFO_CREATE_SUPPORTS_STARTUP_NOTIFICATION,
                                                       &error);
        if (app_info == NULL) {
                g_warning ("failed to create application info: %s",
                           error->message);
                g_error_free (error);
                goto out;
        }

        /* launch gnome-control-center */
        ret = g_app_info_launch (app_info,
                                 NULL,
                                 G_APP_LAUNCH_CONTEXT (launch_context),
                                 &error);
        if (!ret) {
                g_warning ("failed to launch gnome-control-center: %s",
                           error->message);
                g_error_free (error);
                goto out;
        }
out:
        g_object_unref (launch_context);
        if (app_info != NULL)
                g_object_unref (app_info);
}

static void
gcm_session_notify_cb (NotifyNotification *notification,
                       gchar *action,
                       gpointer user_data)
{
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
                gcm_session_exec_control_center (manager);
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

typedef struct {
        GsdColorManager         *manager;
        CdDevice                *device;
} GsdColorManagerDeviceHelper;

static void
gcm_session_profile_connect_cb (GObject *object,
                                GAsyncResult *res,
                                gpointer user_data)
{
        const gchar *filename;
        gboolean allow_notifications;
        gboolean ret;
        gchar *basename = NULL;
        GError *error = NULL;
        CdProfile *profile = CD_PROFILE (object);
        GsdColorManagerDeviceHelper *helper = (GsdColorManagerDeviceHelper *) user_data;
        GsdColorManager *manager = GSD_COLOR_MANAGER (helper->manager);

        ret = cd_profile_connect_sync (profile,
                                       NULL,
                                       &error);
        if (!ret) {
                g_warning ("failed to connect to profile: %s",
                           error->message);
                g_error_free (error);
                goto out;
        }

        /* ensure it's a profile generated by us */
        filename = cd_profile_get_filename (profile);
        basename = g_path_get_basename (filename);
        if (!g_str_has_prefix (basename, "GCM")) {
                g_debug ("not a GCM profile for %s: %s",
                         cd_device_get_id (helper->device), filename);
                goto out;
        }

        /* do we allow notifications */
        allow_notifications = g_settings_get_boolean (manager->priv->settings,
                                                      GCM_SETTINGS_SHOW_NOTIFICATIONS);
        if (!allow_notifications)
                goto out;

        /* handle device */
        gcm_session_notify_device (manager, helper->device);
out:
        g_object_unref (helper->device);
        g_object_unref (helper->manager);
        g_free (helper);
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
        GsdColorManager *manager = GSD_COLOR_MANAGER (user_data);
        GsdColorManagerDeviceHelper *helper;

        ret = cd_device_connect_sync (device,
                                      NULL,
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
                return;

        /* ensure we have a profile */
        profile = cd_device_get_default_profile (device);
        if (profile == NULL) {
                g_debug ("no profile set for %s", cd_device_get_id (device));
                goto out;
        }

        /* connect to the profile */
        helper = g_new0 (GsdColorManagerDeviceHelper, 1);
        helper->manager = g_object_ref (manager);
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
                                    GsdColorManager *manager)
{
        /* connect to the device to get properties */
        cd_device_connect (device,
                           NULL,
                           gcm_session_device_connect_cb,
                           manager);
}

#ifdef HAVE_LCMS
static gchar *
gcm_session_get_precooked_md5 (cmsHPROFILE lcms_profile)
{
        cmsUInt8Number profile_id[16];
        gboolean md5_precooked = FALSE;
        guint i;
        gchar *md5 = NULL;

        /* check to see if we have a pre-cooked MD5 */
        cmsGetHeaderProfileID (lcms_profile, profile_id);
        for (i=0; i<16; i++) {
                if (profile_id[i] != 0) {
                        md5_precooked = TRUE;
                        break;
                }
        }
        if (!md5_precooked)
                goto out;

        /* convert to a hex string */
        md5 = g_new0 (gchar, 32 + 1);
        for (i=0; i<16; i++)
                g_snprintf (md5 + i*2, 3, "%02x", profile_id[i]);
out:
        return md5;
}
#endif

static gchar *
gcm_session_get_md5_for_filename (const gchar *filename,
                                  GError **error)
{
        gboolean ret;
        gchar *checksum = NULL;
        gchar *data = NULL;
        gsize length;

#ifdef HAVE_LCMS
        cmsHPROFILE lcms_profile = NULL;

        /* get the internal profile id, if it exists */
        lcms_profile = cmsOpenProfileFromFile (filename, "r");
        if (lcms_profile == NULL) {
                g_set_error_literal (error, 1, 0,
                                     "failed to load: not an ICC profile");
                goto out;
        }
        checksum = gcm_session_get_precooked_md5 (lcms_profile);
        if (checksum != NULL)
                goto out;
#endif

        /* generate checksum */
        ret = g_file_get_contents (filename, &data, &length, error);
        if (!ret)
                goto out;
        checksum = g_compute_checksum_for_data (G_CHECKSUM_MD5,
                                                (const guchar *) data,
                                                length);
out:
        g_free (data);
#ifdef HAVE_LCMS
        if (lcms_profile != NULL)
                cmsCloseProfile (lcms_profile);
#endif
        return checksum;
}

static void
gcm_session_create_profile_cb (GObject *object,
                               GAsyncResult *res,
                               gpointer user_data)
{
        CdProfile *profile;
        GError *error = NULL;
        CdClient *client = CD_CLIENT (object);

        profile = cd_client_create_profile_finish (client, res, &error);
        if (profile == NULL) {
                g_warning ("%s", error->message);
                g_error_free (error);
                return;
        }
        g_object_unref (profile);
}

static void
gcm_session_profile_store_added_cb (GcmProfileStore *profile_store,
                                    const gchar *filename,
                                    GsdColorManager *manager)
{
        CdProfile *profile = NULL;
        gchar *checksum = NULL;
        gchar *profile_id = NULL;
        GError *error = NULL;
        GHashTable *profile_props = NULL;
        GsdColorManagerPrivate *priv = manager->priv;

        g_debug ("profile %s added", filename);

        /* generate ID */
        checksum = gcm_session_get_md5_for_filename (filename, &error);
        if (checksum == NULL) {
                g_warning ("failed to get profile checksum: %s",
                           error->message);
                g_error_free (error);
                goto out;
        }
        profile_id = g_strdup_printf ("icc-%s", checksum);
        profile_props = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               NULL, NULL);
        g_hash_table_insert (profile_props,
                             CD_PROFILE_PROPERTY_FILENAME,
                             (gpointer) filename);
        g_hash_table_insert (profile_props,
                             CD_PROFILE_METADATA_FILE_CHECKSUM,
                             (gpointer) checksum);
        cd_client_create_profile (priv->client,
                                  profile_id,
                                  CD_OBJECT_SCOPE_TEMP,
                                  profile_props,
                                  NULL,
                                  gcm_session_create_profile_cb,
                                  manager);
out:
        g_free (checksum);
        g_free (profile_id);
        if (profile_props != NULL)
                g_hash_table_unref (profile_props);
        if (profile != NULL)
                g_object_unref (profile);
}

static void
gcm_session_delete_profile_cb (GObject *object,
                               GAsyncResult *res,
                               gpointer user_data)
{
        gboolean ret;
        GError *error = NULL;
        CdClient *client = CD_CLIENT (object);

        ret = cd_client_delete_profile_finish (client, res, &error);
        if (!ret) {
                g_warning ("%s", error->message);
                g_error_free (error);
        }
}

static void
gcm_session_find_profile_by_filename_cb (GObject *object,
                                         GAsyncResult *res,
                                         gpointer user_data)
{
        GError *error = NULL;
        CdProfile *profile;
        CdClient *client = CD_CLIENT (object);
        GsdColorManager *manager = GSD_COLOR_MANAGER (user_data);

        profile = cd_client_find_profile_by_filename_finish (client, res, &error);
        if (profile == NULL) {
                g_warning ("%s", error->message);
                g_error_free (error);
                goto out;
        }

        /* remove it from colord */
        g_debug ("profile %s removed", cd_profile_get_id (profile));
        cd_client_delete_profile (manager->priv->client,
                                  profile,
                                  NULL,
                                  gcm_session_delete_profile_cb,
                                  manager);
out:
        if (profile != NULL)
                g_object_unref (profile);
}

static void
gcm_session_profile_store_removed_cb (GcmProfileStore *profile_store,
                                      const gchar *filename,
                                      GsdColorManager *manager)
{
        /* find the ID for the filename */
        g_debug ("filename %s removed", filename);
        cd_client_find_profile_by_filename (manager->priv->client,
                                            filename,
                                            NULL,
                                            gcm_session_find_profile_by_filename_cb,
                                            manager);
}

static void
gcm_session_sensor_added_cb (CdClient *client,
                             CdSensor *sensor,
                             GsdColorManager *manager)
{
#ifdef HAVE_LIBCANBERRA
        ca_context_play (ca_gtk_context_get (), 0,
                         CA_PROP_EVENT_ID, "device-added",
                         /* TRANSLATORS: this is the application name */
                         CA_PROP_APPLICATION_NAME, _("GNOME Settings Daemon Color Plugin"),
                        /* TRANSLATORS: this is a sound description */
                         CA_PROP_EVENT_DESCRIPTION, _("Color calibration device added"), NULL);
#endif

        /* open up the color prefs window */
        gcm_session_exec_control_center (manager);
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

        /* have access to all user profiles */
        priv->profile_store = gcm_profile_store_new ();
        g_signal_connect (priv->profile_store, "added",
                          G_CALLBACK (gcm_session_profile_store_added_cb),
                          manager);
        g_signal_connect (priv->profile_store, "removed",
                          G_CALLBACK (gcm_session_profile_store_removed_cb),
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
        g_object_unref (manager->priv->profile_store);

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
