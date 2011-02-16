/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
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

#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <packagekit-glib2/packagekit.h>
#include <libnotify/notify.h>

#include "gsd-enums.h"
#include "gsd-updates-manager.h"
#include "gsd-updates-firmware.h"
#include "gsd-updates-refresh.h"
#include "gsd-updates-common.h"
#include "gnome-settings-profile.h"

#define GSD_UPDATES_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_UPDATES_MANAGER, GsdUpdatesManagerPrivate))

struct GsdUpdatesManagerPrivate
{
        GCancellable            *cancellable;
        GsdUpdatesRefresh       *refresh;
        GsdUpdatesFirmware      *firmware;
        GSettings               *settings_ftp;
        GSettings               *settings_gsd;
        GSettings               *settings_http;
        guint                    number_updates_critical_last_shown;
        guint                    timeout;
        NotifyNotification      *notification_updates_available;
        PkControl               *control;
        PkTask                  *task;
};

static void gsd_updates_manager_class_init (GsdUpdatesManagerClass *klass);
static void gsd_updates_manager_init (GsdUpdatesManager *updates_manager);
static void gsd_updates_manager_finalize (GObject *object);
static void update_packages_finished_cb (PkTask *task, GAsyncResult *res, GsdUpdatesManager *manager);

G_DEFINE_TYPE (GsdUpdatesManager, gsd_updates_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static void
libnotify_action_cb (NotifyNotification *notification,
                     gchar *action,
                     gpointer user_data)
{
        gboolean ret;
        GError *error = NULL;
        GsdUpdatesManager *manager = GSD_UPDATES_MANAGER (user_data);

        if (g_strcmp0 (action, "distro-upgrade-info") == 0) {
                ret = g_spawn_command_line_async (DATADIR "/PackageKit/pk-upgrade-distro.sh",
                                                  &error);
                if (!ret) {
                        g_warning ("Failure launching pk-upgrade-distro.sh: %s",
                                   error->message);
                        g_error_free (error);
                }
                goto out;
        }
        if (g_strcmp0 (action, "show-update-viewer") == 0) {
                ret = g_spawn_command_line_async (BINDIR "/gpk-update-viewer",
                                                  &error);
                if (!ret) {
                        g_warning ("Failure launching update viewer: %s",
                                   error->message);
                        g_error_free (error);
                }
                goto out;
        }
        if (g_strcmp0 (action, "update-all-packages") == 0) {
                pk_task_update_system_async (manager->priv->task,
                                             manager->priv->cancellable,
                                             NULL, NULL,
                                             (GAsyncReadyCallback) update_packages_finished_cb,
                                             manager);
                goto out;
        }
        if (g_strcmp0 (action, "cancel") == 0) {
                /* try to cancel */
                g_cancellable_cancel (manager->priv->cancellable);
                goto out;
        }
        g_warning ("unknown action id: %s", action);
out:
        return;
}

static void
get_distro_upgrades_finished_cb (GObject *object,
                                 GAsyncResult *res,
                                 GsdUpdatesManager *manager)
{
        const gchar *title;
        gboolean ret;
        gchar *name = NULL;
        GError *error = NULL;
        GPtrArray *array = NULL;
        GString *string = NULL;
        guint i;
        NotifyNotification *notification;
        PkClient *client = PK_CLIENT(object);
        PkDistroUpgrade *item;
        PkError *error_code = NULL;
        PkResults *results;
        PkUpdateStateEnum state;

        /* get the results */
        results = pk_client_generic_finish (PK_CLIENT(client), res, &error);
        if (results == NULL) {
                g_warning ("failed to get upgrades: %s",
                           error->message);
                g_error_free (error);
                goto out;
        }

        /* check error code */
        error_code = pk_results_get_error_code (results);
        if (error_code != NULL) {
                g_warning ("failed to get upgrades: %s, %s",
                           pk_error_enum_to_text (pk_error_get_code (error_code)),
                           pk_error_get_details (error_code));
                goto out;
        }

        /* process results */
        array = pk_results_get_distro_upgrade_array (results);

        /* any updates? */
        if (array->len == 0) {
                g_debug ("no upgrades");
                goto out;
        }

        /* do we do the notification? */
        ret = g_settings_get_boolean (manager->priv->settings_gsd,
                                      GSD_SETTINGS_NOTIFY_DISTRO_UPGRADES);
        if (!ret) {
                g_debug ("ignoring due to GSettings");
                goto out;
        }

        /* find the upgrade string */
        string = g_string_new ("");
        for (i=0; i < array->len; i++) {
                item = (PkDistroUpgrade *) g_ptr_array_index (array, i);
                g_object_get (item,
                              "name", &name,
                              "state", &state,
                              NULL);
                g_string_append_printf (string, "%s (%s)\n",
                                        name,
                                        pk_distro_upgrade_enum_to_text (state));
                g_free (name);
        }
        if (string->len != 0)
                g_string_set_size (string, string->len-1);

        /* TRANSLATORS: a distro update is available, e.g. Fedora 8 to Fedora 9 */
        title = _("Distribution upgrades available");
        notification = notify_notification_new (title, string->str, NULL);
        if (notification == NULL) {
                g_warning ("failed to create notification");
                goto out;
        }
        notify_notification_set_timeout (notification, NOTIFY_EXPIRES_NEVER);
        notify_notification_set_urgency (notification, NOTIFY_URGENCY_NORMAL);
        notify_notification_add_action (notification, "distro-upgrade-info",
                                        /* TRANSLATORS: provides more information about the upgrade */
                                        _("More information"),
                                        libnotify_action_cb,
                                        manager, NULL);
        ret = notify_notification_show (notification, &error);
        if (!ret) {
                g_warning ("error: %s", error->message);
                g_error_free (error);
        }
out:
        if (error_code != NULL)
                g_object_unref (error_code);
        if (array != NULL)
                g_ptr_array_unref (array);
        if (string != NULL)
                g_string_free (string, TRUE);
        if (results != NULL)
                g_object_unref (results);
}

static void
due_get_upgrades_cb (GsdUpdatesRefresh *refresh, GsdUpdatesManager *manager)
{
        /* optimize the amount of downloaded data by setting the cache age */
        pk_client_set_cache_age (PK_CLIENT(manager->priv->task),
                                 g_settings_get_int (manager->priv->settings_gsd,
                                                     GSD_SETTINGS_FREQUENCY_GET_UPGRADES));

        /* get new distro upgrades list */
        pk_client_get_distro_upgrades_async (PK_CLIENT(manager->priv->task),
                                             NULL,
                                             NULL, NULL,
                                             (GAsyncReadyCallback) get_distro_upgrades_finished_cb,
                                             manager);
}

static void
refresh_cache_finished_cb (GObject *object, GAsyncResult *res, GsdUpdatesManager *manager)
{
        PkClient *client = PK_CLIENT(object);
        PkResults *results;
        GError *error = NULL;
        PkError *error_code = NULL;

        /* get the results */
        results = pk_client_generic_finish (PK_CLIENT(client), res, &error);
        if (results == NULL) {
                g_warning ("failed to refresh the cache: %s",
                           error->message);
                g_error_free (error);
                goto out;
        }

        /* check error code */
        error_code = pk_results_get_error_code (results);
        if (error_code != NULL) {
                g_warning ("failed to refresh the cache: %s, %s",
                           pk_error_enum_to_text (pk_error_get_code (error_code)),
                           pk_error_get_details (error_code));
                goto out;
        }
out:
        if (error_code != NULL)
                g_object_unref (error_code);
        if (results != NULL)
                g_object_unref (results);
}

static void
due_refresh_cache_cb (GsdUpdatesRefresh *refresh, GsdUpdatesManager *manager)
{
        /* optimize the amount of downloaded data by setting the cache age */
        pk_client_set_cache_age (PK_CLIENT(manager->priv->task),
                                 g_settings_get_int (manager->priv->settings_gsd,
                                                     GSD_SETTINGS_FREQUENCY_REFRESH_CACHE));

        pk_client_refresh_cache_async (PK_CLIENT(manager->priv->task),
                                       TRUE,
                                       NULL,
                                       NULL, NULL,
                                       (GAsyncReadyCallback) refresh_cache_finished_cb,
                                       manager);
}

static void
notify_critical_updates (GsdUpdatesManager *manager, GPtrArray *array)
{
        const gchar *message;
        const gchar *title;
        gboolean ret;
        GError *error = NULL;
        GsdUpdateType update_type;
        NotifyNotification *notification;

        /* do we do the notification? */
        update_type = g_settings_get_enum (manager->priv->settings_gsd,
                                           GSD_SETTINGS_NOTIFY_UPDATE_TYPE);
        if (update_type == GSD_UPDATE_TYPE_NONE) {
                g_debug ("ignoring due to GSettings");
                return;
        }

        /* if the number of critical updates is the same as the last notification,
         * then skip the notifcation as we don't want to bombard the user every hour */
        if (array->len == manager->priv->number_updates_critical_last_shown) {
                g_debug ("ignoring as user ignored last warning");
                return;
        }

        /* save for comparison later */
        manager->priv->number_updates_critical_last_shown = array->len;

        /* TRANSLATORS: title in the libnotify popup */
        title = ngettext ("Security update available", "Security updates available", array->len);

        /* TRANSLATORS: message when there are security updates */
        message = ngettext ("An important update is available for your computer:",
                            "Important updates are available for your computer:", array->len);

        /* close any existing notification */
        if (manager->priv->notification_updates_available != NULL) {
                notify_notification_close (manager->priv->notification_updates_available, NULL);
                manager->priv->notification_updates_available = NULL;
        }

        /* do the bubble */
        g_debug ("title=%s, message=%s", title, message);
        notification = notify_notification_new (title, message, NULL);
        if (notification == NULL) {
                g_warning ("failed to get bubble");
                return;
        }
        notify_notification_set_timeout (notification, 15000);
        notify_notification_set_urgency (notification, NOTIFY_URGENCY_CRITICAL);
        notify_notification_add_action (notification, "show-update-viewer",
                                        /* TRANSLATORS: button: open the update viewer to install updates*/
                                        _("Install updates"), libnotify_action_cb, manager, NULL);
        ret = notify_notification_show (notification, &error);
        if (!ret) {
                g_warning ("error: %s", error->message);
                g_error_free (error);
        }
        /* track so we can prevent doubled notifications */
        manager->priv->notification_updates_available = notification;
}

static gboolean
update_check_on_battery (GsdUpdatesManager *manager)
{
        const gchar *message;
        gboolean ret;
        GError *error = NULL;
        NotifyNotification *notification;

        ret = g_settings_get_boolean (manager->priv->settings_gsd,
                                      GSD_SETTINGS_UPDATE_BATTERY);
        if (ret) {
                g_debug ("okay to update due to policy");
                return TRUE;
        }

        ret = gsd_updates_refresh_get_on_battery (manager->priv->refresh);
        if (!ret) {
                g_debug ("okay to update as on AC");
                return TRUE;
        }

        /* do we do the notification? */
        ret = g_settings_get_boolean (manager->priv->settings_gsd,
                                      GSD_SETTINGS_NOTIFY_UPDATE_NOT_BATTERY);
        if (!ret) {
                g_debug ("ignoring due to GSettings");
                return FALSE;
        }

        /* TRANSLATORS: policy says update, but we are on battery and so prompt */
        message = _("Automatic updates are not being installed as the computer is running on battery power");
        /* TRANSLATORS: informs user will not install by default */
        notification = notify_notification_new (_("Updates not installed"),
                                                message, NULL);
        notify_notification_set_timeout (notification, 15000);
        notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
        notify_notification_add_action (notification, "update-all-packages",
                                        /* TRANSLATORS: to hell with my battery life, just do it */
                                        _("Install the updates anyway"), libnotify_action_cb, manager, NULL);
        ret = notify_notification_show (notification, &error);
        if (!ret) {
                g_warning ("error: %s", error->message);
                g_error_free (error);
        }

        return FALSE;
}

static const gchar *
restart_enum_to_localised_text (PkRestartEnum restart)
{
        const gchar *text = NULL;
        switch (restart) {
        case PK_RESTART_ENUM_NONE:
                text = _("No restart is required.");
                break;
        case PK_RESTART_ENUM_SYSTEM:
                text = _("A restart is required.");
                break;
        case PK_RESTART_ENUM_SESSION:
                text = _("You need to log out and log back in.");
                break;
        case PK_RESTART_ENUM_APPLICATION:
                text = _("You need to restart the application.");
                break;
        case PK_RESTART_ENUM_SECURITY_SESSION:
                text = _("You need to log out and log back in to remain secure.");
                break;
        case PK_RESTART_ENUM_SECURITY_SYSTEM:
                text = _("A restart is required to remain secure.");
                break;
        default:
                g_warning ("restart unrecognised: %i", restart);
        }
        return text;
}

static void
notify_update_finished (GsdUpdatesManager *manager, PkResults *results)
{
        const gchar *message;
        gboolean ret;
        gchar *package_id = NULL;
        gchar **split;
        gchar *summary = NULL;
        GError *error = NULL;
        GPtrArray *array;
        GString *message_text = NULL;
        guint i;
        guint skipped_number = 0;
        NotifyNotification *notification;
        PkInfoEnum info;
        PkPackage *item;
        PkRestartEnum restart;

        /* check we got some packages */
        array = pk_results_get_package_array (results);
        g_debug ("length=%i", array->len);
        if (array->len == 0) {
                g_debug ("no updates");
                goto out;
        }

        message_text = g_string_new ("");

        /* find any we skipped */
        for (i=0; i<array->len; i++) {
                item = g_ptr_array_index (array, i);
                g_object_get (item,
                              "info", &info,
                              "package-id", &package_id,
                              "summary", &summary,
                              NULL);

                split = pk_package_id_split (package_id);
                g_debug ("%s, %s, %s", pk_info_enum_to_text (info),
                           split[PK_PACKAGE_ID_NAME], summary);
                if (info == PK_INFO_ENUM_BLOCKED) {
                        skipped_number++;
                        g_string_append_printf (message_text, "<b>%s</b> - %s\n",
                                                split[PK_PACKAGE_ID_NAME], summary);
                }
                g_free (package_id);
                g_free (summary);
                g_strfreev (split);
        }

        /* notify the user if there were skipped entries */
        if (skipped_number > 0) {
                /* TRANSLATORS: we did the update, but some updates were skipped and not applied */
                message = ngettext ("One package was skipped:",
                                    "Some packages were skipped:", skipped_number);
                g_string_prepend (message_text, message);
                g_string_append_c (message_text, '\n');
        }

        /* add a message that we need to restart */
        restart = pk_results_get_require_restart_worst (results);
        if (restart != PK_RESTART_ENUM_NONE) {
                message = restart_enum_to_localised_text (restart);

                /* add a gap if we are putting both */
                if (skipped_number > 0)
                        g_string_append (message_text, "\n");

                g_string_append (message_text, message);
                g_string_append_c (message_text, '\n');
        }

        /* trim off extra newlines */
        if (message_text->len != 0)
                g_string_set_size (message_text, message_text->len-1);

        /* do we do the notification? */
        ret = g_settings_get_boolean (manager->priv->settings_gsd,
                                      GSD_SETTINGS_NOTIFY_UPDATE_COMPLETE);
        if (!ret) {
                g_debug ("ignoring due to GSettings");
                goto out;
        }

        /* TRANSLATORS: title: system update completed all okay */
        notification = notify_notification_new (_("The system update has completed"),
                                                 message_text->str, NULL);
        notify_notification_set_timeout (notification, 15000);
        notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
        if (restart == PK_RESTART_ENUM_SYSTEM) {
                notify_notification_add_action (notification, "restart",
                                                /* TRANSLATORS: restart computer as system packages need update */
                                                _("Restart computer now"),
                                                libnotify_action_cb,
                                                manager, NULL);
        }
        ret = notify_notification_show (notification, &error);
        if (!ret) {
                g_warning ("error: %s", error->message);
                g_error_free (error);
        }
out:
        if (message_text != NULL)
                g_string_free (message_text, TRUE);
        if (array != NULL)
                g_ptr_array_unref (array);
}

static void
update_packages_finished_cb (PkTask *task,
                             GAsyncResult *res,
                             GsdUpdatesManager *manager)
{
        PkResults *results;
        GError *error = NULL;
        PkError *error_code = NULL;

        /* get the results */
        results = pk_task_generic_finish (task, res, &error);
        if (results == NULL) {
                g_warning ("failed to update system: %s",
                           error->message);
                g_error_free (error);
                goto out;
        }

        /* check error code */
        error_code = pk_results_get_error_code (results);
        if (error_code != NULL) {
                g_warning ("failed to update system: %s, %s",
                           pk_error_enum_to_text (pk_error_get_code (error_code)),
                           pk_error_get_details (error_code));
                goto out;
        }

        /* notify */
        notify_update_finished (manager, results);
        manager->priv->number_updates_critical_last_shown = 0;
out:
        if (error_code != NULL)
                g_object_unref (error_code);
        if (results != NULL)
                g_object_unref (results);
}

static void
notify_doing_updates (GsdUpdatesManager *manager)
{
        gboolean ret;
        GError *error = NULL;
        GsdUpdateType update_type;
        NotifyNotification *notification;

        /* in GSettings? */
        update_type = g_settings_get_enum (manager->priv->settings_gsd,
                                           GSD_SETTINGS_NOTIFY_UPDATE_TYPE);
        if (update_type == GSD_UPDATE_TYPE_SECURITY ||
            update_type == GSD_UPDATE_TYPE_NONE) {
                g_debug ("ignoring due to GSettings");
                goto out;
        }

        /* TRANSLATORS: title: notification when we scheduled an automatic update */
        notification = notify_notification_new (_("Updates are being installed"),
                                                /* TRANSLATORS: tell the user why the hard disk is grinding... */
                                                _("Updates are being automatically installed on your computer"),
                                                "software-update-urgent");
        notify_notification_set_timeout (notification, 15000);
        notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
        /* TRANSLATORS: button: cancel the update system */
        notify_notification_add_action (notification, "cancel",
                                        _("Cancel update"),
                                        libnotify_action_cb,
                                        manager, NULL);
        ret = notify_notification_show (notification, &error);
        if (!ret) {
                g_warning ("error: %s", error->message);
                g_error_free (error);
        }
out:
        return;
}

static void
get_updates_finished_cb (GObject *object, GAsyncResult *res, GsdUpdatesManager *manager)
{
        PkClient *client = PK_CLIENT(object);
        PkResults *results;
        GError *error = NULL;
        PkPackage *item;
        guint i;
        gboolean ret;
        GsdUpdateType update;
        GPtrArray *security_array = NULL;
        gchar **package_ids;
        GPtrArray *array = NULL;
        PkError *error_code = NULL;

        /* get the results */
        results = pk_client_generic_finish (PK_CLIENT(client), res, &error);
        if (results == NULL) {
                g_warning ("failed to get updates: %s", error->message);
                g_error_free (error);
                goto out;
        }

        /* check error code */
        error_code = pk_results_get_error_code (results);
        if (error_code != NULL) {
                g_warning ("failed to get updates: %s, %s",
                           pk_error_enum_to_text (pk_error_get_code (error_code)),
                           pk_error_get_details (error_code));
                goto out;
        }

        /* get data */
        array = pk_results_get_package_array (results);

        /* we have no updates */
        if (array->len == 0) {
                g_debug ("no updates");
                goto out;
        }

        /* we have updates to process */
        security_array = g_ptr_array_new_with_free_func (g_free);

        /* find the security updates first */
        for (i=0; i<array->len; i++) {
                item = g_ptr_array_index (array, i);
                if (pk_package_get_info (item) != PK_INFO_ENUM_SECURITY)
                        continue;
                g_ptr_array_add (security_array, g_strdup (pk_package_get_id (item)));
        }

        /* do we do the automatic updates? */
        update = g_settings_get_enum (manager->priv->settings_gsd,
                                      GSD_SETTINGS_AUTO_UPDATE_TYPE);

        /* is policy none? */
        if (update == GSD_UPDATE_TYPE_NONE) {
                g_debug ("not updating as policy NONE");
                /* do we warn the user? */
                if (security_array->len > 0)
                        notify_critical_updates (manager, security_array);
                goto out;
        }

        /* are we on battery and configured to skip the action */
        ret = update_check_on_battery (manager);
        if (!ret &&
            ((update == GSD_UPDATE_TYPE_SECURITY && security_array->len > 0) ||
              update == GSD_UPDATE_TYPE_ALL)) {
                g_debug ("on battery so not doing update");
                if (security_array->len > 0)
                        notify_critical_updates (manager, security_array);
                goto out;
        }

        /* just do security updates */
        if (update == GSD_UPDATE_TYPE_SECURITY) {
                if (security_array->len == 0) {
                        g_debug ("policy security, but none available");
                        goto out;
                }

                /* convert */
                package_ids = pk_ptr_array_to_strv (security_array);
                pk_task_update_packages_async (manager->priv->task, package_ids,
                                               manager->priv->cancellable,
                                               NULL, NULL,
                                               (GAsyncReadyCallback) update_packages_finished_cb, manager);
                notify_doing_updates (manager);
                g_strfreev (package_ids);
                goto out;
        }

        /* just do everything */
        if (update == GSD_UPDATE_TYPE_ALL) {
                g_debug ("we should do the update automatically!");
                pk_task_update_system_async (manager->priv->task,
                                             manager->priv->cancellable,
                                             NULL, NULL,
                                             (GAsyncReadyCallback) update_packages_finished_cb,
                                             manager);
                notify_doing_updates (manager);
                goto out;
        }

        /* shouldn't happen */
        g_warning ("unknown update mode");
out:
        if (error_code != NULL)
                g_object_unref (error_code);
        if (security_array != NULL)
                g_ptr_array_unref (security_array);
        if (array != NULL)
                g_ptr_array_unref (array);
        if (results != NULL)
                g_object_unref (results);
}

static void
due_get_updates_cb (GsdUpdatesRefresh *refresh, GsdUpdatesManager *manager)
{
        /* optimize the amount of downloaded data by setting the cache age */
        pk_client_set_cache_age (PK_CLIENT(manager->priv->task),
                                 g_settings_get_int (manager->priv->settings_gsd,
                                                     GSD_SETTINGS_FREQUENCY_GET_UPDATES));

        /* get new update list */
        pk_client_get_updates_async (PK_CLIENT(manager->priv->task),
                                     pk_bitfield_value (PK_FILTER_ENUM_NONE),
                                     manager->priv->cancellable,
                                     NULL, NULL,
                                     (GAsyncReadyCallback) get_updates_finished_cb,
                                     manager);
}

static gchar *
get_proxy_http (GsdUpdatesManager *manager)
{
        gboolean ret;
        gchar *host = NULL;
        gchar *password = NULL;
        gchar *proxy = NULL;
        gchar *username = NULL;
        GString *string = NULL;
        guint port;

        ret = g_settings_get_boolean (manager->priv->settings_http,
                                      "enabled");
        if (!ret)
                goto out;

        host = g_settings_get_string (manager->priv->settings_http,
                                      "host");
        if (host == NULL)
                goto out;
        port = g_settings_get_int (manager->priv->settings_http,
                                   "port");

        /* use an HTTP auth string? */
        ret = g_settings_get_boolean (manager->priv->settings_http,
                                      "use-authentication");
        if (ret) {
                username = g_settings_get_string (manager->priv->settings_http,
                                                  "authentication-user");
                password = g_settings_get_string (manager->priv->settings_http,
                                                  "authentication-password");
        }

        /* make PackageKit proxy string */
        string = g_string_new (host);
        if (port > 0)
                g_string_append_printf (string, ":%i", port);
        if (username != NULL && password != NULL)
                g_string_append_printf (string, "@%s:%s", username, password);
        else if (username != NULL)
                g_string_append_printf (string, "@%s", username);
        else if (password != NULL)
                g_string_append_printf (string, "@:%s", password);
        proxy = g_string_free (string, FALSE);
out:
        g_free (host);
        g_free (username);
        g_free (password);
        return proxy;
}

static gchar *
get_proxy_ftp (GsdUpdatesManager *manager)
{
        gboolean ret;
        gchar *host = NULL;
        gchar *proxy = NULL;
        GString *string = NULL;
        guint port;

        ret = g_settings_get_boolean (manager->priv->settings_http,
                                      "enabled");
        if (!ret)
                goto out;

        host = g_settings_get_string (manager->priv->settings_http,
                                      "host");
        if (host == NULL)
                goto out;
        port = g_settings_get_int (manager->priv->settings_http,
                                   "port");

        /* make PackageKit proxy string */
        string = g_string_new (host);
        if (port > 0)
                g_string_append_printf (string, ":%i", port);
        proxy = g_string_free (string, FALSE);
out:
        g_free (host);
        return proxy;
}


static void
set_proxy_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
        gboolean ret;
        GError *error = NULL;
        PkControl *control = PK_CONTROL (object);

        /* get the result */
        ret = pk_control_set_proxy_finish (control, res, &error);
        if (!ret) {
                g_warning ("failed to set proxies: %s", error->message);
                g_error_free (error);
        }
}

static void
reload_proxy_settings (GsdUpdatesManager *manager)
{
        gchar *proxy_http;
        gchar *proxy_ftp;

        proxy_http = get_proxy_http (manager);
        proxy_ftp = get_proxy_ftp (manager);

        /* send to daemon */
        pk_control_set_proxy_async (manager->priv->control,
                                    proxy_http,
                                    proxy_ftp,
                                    NULL,
                                    set_proxy_cb,
                                    manager);

        g_free (proxy_http);
        g_free (proxy_ftp);
}

static void
set_install_root_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
        gboolean ret;
        GError *error = NULL;
        PkControl *control = PK_CONTROL (object);

        /* get the result */
        ret = pk_control_set_root_finish (control, res, &error);
        if (!ret) {
                g_warning ("failed to set install root: %s", error->message);
                g_error_free (error);
        }
}

static void
set_install_root (GsdUpdatesManager *manager)
{
        gchar *root;

        /* get install root */
        root = g_settings_get_string (manager->priv->settings_gsd,
                                      "install-root");
        if (root == NULL) {
                g_warning ("could not read install root");
                goto out;
        }

        pk_control_set_root_async (manager->priv->control,
                                   root,
                                   NULL,
                                   set_install_root_cb, manager);
out:
        g_free (root);
}

static void
settings_changed_cb (GSettings         *settings,
                     const char        *key,
                     GsdUpdatesManager *manager)
{
        reload_proxy_settings (manager);
}

static void
settings_gsd_changed_cb (GSettings         *settings,
                         const char        *key,
                         GsdUpdatesManager *manager)
{
        set_install_root (manager);
}

gboolean
gsd_updates_manager_start (GsdUpdatesManager *manager,
                           GError **error)
{
        /* use PackageKit */
        manager->priv->cancellable = g_cancellable_new ();
        manager->priv->control = pk_control_new ();
        manager->priv->task = pk_task_new ();
        g_object_set (manager->priv->task,
                      "background", TRUE,
                      "interactive", FALSE,
                      NULL);

        /* watch UDev for missing firmware */
        manager->priv->firmware = gsd_updates_firmware_new ();

        /* get automatic callbacks about when we should check for
         * updates, refresh-caches and upgrades */
        manager->priv->refresh = gsd_updates_refresh_new ();
        g_signal_connect (manager->priv->refresh, "get-upgrades",
                          G_CALLBACK (due_get_upgrades_cb), manager);
        g_signal_connect (manager->priv->refresh, "refresh-cache",
                          G_CALLBACK (due_refresh_cache_cb), manager);
        g_signal_connect (manager->priv->refresh, "get-updates",
                          G_CALLBACK (due_get_updates_cb), manager);

        /* get http settings */
        manager->priv->settings_http = g_settings_new ("org.gnome.system.proxy.http");
        g_signal_connect (manager->priv->settings_http, "changed",
                          G_CALLBACK (settings_changed_cb), manager);

        /* get ftp settings */
        manager->priv->settings_ftp = g_settings_new ("org.gnome.system.proxy.ftp");
        g_signal_connect (manager->priv->settings_ftp, "changed",
                          G_CALLBACK (settings_changed_cb), manager);

        /* get ftp settings */
        manager->priv->settings_gsd = g_settings_new ("org.gnome.settings-daemon.plugins.updates");
        g_signal_connect (manager->priv->settings_gsd, "changed",
                          G_CALLBACK (settings_gsd_changed_cb), manager);

        /* coldplug */
        reload_proxy_settings (manager);
        set_install_root (manager);

        g_debug ("Starting updates manager");

        return TRUE;
}

void
gsd_updates_manager_stop (GsdUpdatesManager *manager)
{
        g_debug ("Stopping updates manager");

        if (manager->priv->settings_http != NULL) {
                g_object_unref (manager->priv->settings_http);
                manager->priv->settings_http = NULL;
        }
        if (manager->priv->settings_ftp != NULL) {
                g_object_unref (manager->priv->settings_ftp);
                manager->priv->settings_ftp = NULL;
        }
        if (manager->priv->settings_gsd != NULL) {
                g_object_unref (manager->priv->settings_gsd);
                manager->priv->settings_gsd = NULL;
        }
        if (manager->priv->control != NULL) {
                g_object_unref (manager->priv->control);
                manager->priv->control = NULL;
        }
        if (manager->priv->task != NULL) {
                g_object_unref (manager->priv->task);
                manager->priv->task = NULL;
        }
        if (manager->priv->refresh != NULL) {
                g_object_unref (manager->priv->refresh);
                manager->priv->refresh = NULL;
        }
        if (manager->priv->firmware != NULL) {
                g_object_unref (manager->priv->firmware);
                manager->priv->firmware = NULL;
        }
        if (manager->priv->cancellable != NULL) {
                g_object_unref (manager->priv->cancellable);
                manager->priv->cancellable = NULL;
        }

        if (manager->priv->timeout) {
                g_source_remove (manager->priv->timeout);
                manager->priv->timeout = 0;
        }
}

static GObject *
gsd_updates_manager_constructor (
                GType type,
                guint n_construct_properties,
                GObjectConstructParam *construct_properties)
{
        GsdUpdatesManager *m;
        GsdUpdatesManagerClass *klass;

        klass = GSD_UPDATES_MANAGER_CLASS (g_type_class_peek (GSD_TYPE_UPDATES_MANAGER));

        m = GSD_UPDATES_MANAGER (G_OBJECT_CLASS (gsd_updates_manager_parent_class)->constructor (
                                                           type,
                                                           n_construct_properties,
                                                           construct_properties));

        return G_OBJECT (m);
}

static void
gsd_updates_manager_dispose (GObject *object)
{
        GsdUpdatesManager *manager;

        manager = GSD_UPDATES_MANAGER (object);

        gsd_updates_manager_stop (manager);

        G_OBJECT_CLASS (gsd_updates_manager_parent_class)->dispose (object);
}

static void
gsd_updates_manager_class_init (GsdUpdatesManagerClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->constructor = gsd_updates_manager_constructor;
        object_class->dispose = gsd_updates_manager_dispose;
        object_class->finalize = gsd_updates_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdUpdatesManagerPrivate));
}

static void
gsd_updates_manager_init (GsdUpdatesManager *manager)
{
        manager->priv = GSD_UPDATES_MANAGER_GET_PRIVATE (manager);
}

static void
gsd_updates_manager_finalize (GObject *object)
{
        GsdUpdatesManager *updates_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_UPDATES_MANAGER (object));

        updates_manager = GSD_UPDATES_MANAGER (object);

        g_return_if_fail (updates_manager->priv);

        G_OBJECT_CLASS (gsd_updates_manager_parent_class)->finalize (object);
}

GsdUpdatesManager *
gsd_updates_manager_new (void)
{
        if (manager_object) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_UPDATES_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object, (gpointer *) &manager_object);
        }

        return GSD_UPDATES_MANAGER (manager_object);
}
