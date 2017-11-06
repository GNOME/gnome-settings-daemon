/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Red Hat, Inc.
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
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <cups/cups.h>
#include <cups/ppd.h>
#include <libnotify/notify.h>

#include "gnome-settings-profile.h"
#include "gsd-account-manager.h"
#include "org.freedesktop.Accounts.h"
#include "org.freedesktop.Accounts.User.h"

#define GSD_ACCOUNT_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_ACCOUNT_MANAGER, GsdAccountManagerPrivate))

struct GsdAccountManagerPrivate
{
        GsdAccounts          *accounts_proxy;
        GsdAccountsUser      *accounts_user_proxy;
        GCancellable         *cancellable;

        gint64                expiration_time;
        gint64                last_change_time;
        gint64                min_days_between_changes;
        gint64                max_days_between_changes;
        gint64                days_to_warn;
        gint64                days_after_expiration_until_lock;

        NotifyNotification   *notification;
};

static void     gsd_account_manager_class_init  (GsdAccountManagerClass *klass);
static void     gsd_account_manager_init        (GsdAccountManager      *account_manager);
static void     gsd_account_manager_finalize    (GObject                *object);

G_DEFINE_TYPE (GsdAccountManager, gsd_account_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static void
on_notification_closed (NotifyNotification *notification,
                        gpointer            user_data)
{
        GsdAccountManager *manager = user_data;

        g_clear_object (&manager->priv->notification);
}

static void
hide_notification (GsdAccountManager *manager)
{
        if (manager->priv->notification == NULL)
                return;

        notify_notification_close (manager->priv->notification, NULL);
        g_clear_object (&manager->priv->notification);
}

static void
show_notification (GsdAccountManager *manager,
                   const char        *primary_text,
                   const char        *secondary_text)
{
        g_assert (manager->priv->notification == NULL);

        manager->priv->notification = notify_notification_new (primary_text,
                                                               secondary_text,
                                                               "avatar-default-symbolic");
        notify_notification_set_app_name (manager->priv->notification, _("User Account"));
        notify_notification_set_hint (manager->priv->notification,
                                      "resident",
                                      g_variant_new_boolean (TRUE));
        notify_notification_set_timeout (manager->priv->notification,
                                         NOTIFY_EXPIRES_NEVER);

        g_signal_connect (manager->priv->notification,
                          "closed",
                          G_CALLBACK (on_notification_closed),
                          manager);

        notify_notification_show (manager->priv->notification, NULL);
}

static void
update_password_notification (GsdAccountManager *manager)
{
        gint64           days_since_epoch;
        gint64           days_until_expiration = -1;
        gint64           days_since_last_change = -1;
        gint64           days_left = -1;
        g_autofree char *primary_text = NULL;
        g_autofree char *secondary_text = NULL;
        gboolean         password_already_expired = FALSE;

        hide_notification (manager);

        days_since_epoch = g_get_real_time () / G_USEC_PER_SEC / 60 / 60 / 24;

        if (manager->priv->expiration_time > 0) {
                days_until_expiration = manager->priv->expiration_time - days_since_epoch;

                if (days_until_expiration <= 0) {
                        password_already_expired = TRUE;
                        goto out;
                }
        }

        if (manager->priv->last_change_time == 0) {
                password_already_expired = TRUE;
                goto out;
        }

        days_since_last_change = days_since_epoch - manager->priv->last_change_time;

        if (days_since_last_change < 0) {
                /* time skew, password was changed in the future! */
                goto out;
        }

        if (manager->priv->max_days_between_changes > -1) {
                if (manager->priv->days_after_expiration_until_lock > -1) {
                        if ((days_since_last_change > manager->priv->max_days_between_changes) &&
                            (days_since_last_change > manager->priv->days_after_expiration_until_lock)) {
                                password_already_expired = TRUE;
                                goto out;
                        }
                }

                if (days_since_last_change > manager->priv->max_days_between_changes) {
                        password_already_expired = TRUE;
                        goto out;
                }

                if (manager->priv->days_to_warn > -1) {
                        if (days_since_last_change > manager->priv->max_days_between_changes - manager->priv->days_to_warn) {
                                days_left = manager->priv->last_change_time + manager->priv->max_days_between_changes - days_since_epoch;

                                if (days_until_expiration >= 0)
                                        days_left = MIN (days_left, days_until_expiration);
                                goto out;
                        }
                }
        }

out:
        if (password_already_expired) {
                primary_text = g_strdup_printf (_("Password Expired"));
                secondary_text = g_strdup_printf (_("Your password is expired. Please update it."));
        } else if (days_left >= 0) {
                primary_text = g_strdup_printf (_("Password Expiring Soon"));
                if (days_left == 0)
                    secondary_text = g_strdup_printf (_("Your password is expiring today."));
                else if (days_left == 1)
                    secondary_text = g_strdup_printf (_("Your password is expiring in a day."));
                else
                    secondary_text = g_strdup_printf (_("Your password is expiring in %ld days."),
                                                      days_left);
        }

        if (primary_text != NULL && secondary_text != NULL)
                show_notification (manager,
                                   primary_text,
                                   secondary_text);
}

static gboolean
set_policy_number (gint64 *destination,
                   gint64  source)
{
        if (*destination == source)
                return FALSE;

        *destination = source;
        return TRUE;
}

static void
on_got_password_expiration_policy (GsdAccountsUser *accounts_user_proxy,
                                   GAsyncResult    *res,
                                   gpointer         user_data)
{
        GsdAccountManager *manager = user_data;
        g_autoptr(GError)  error = NULL;
        gboolean           succeeded;
        gint64             expiration_time;
        gint64             last_change_time;
        gint64             min_days_between_changes;
        gint64             max_days_between_changes;
        gint64             days_to_warn;
        gint64             days_after_expiration_until_lock;

        gnome_settings_profile_start (NULL);
        succeeded = gsd_accounts_user_call_get_password_expiration_policy_finish (accounts_user_proxy,
                                                                                  &expiration_time,
                                                                                  &last_change_time,
                                                                                  &min_days_between_changes,
                                                                                  &max_days_between_changes,
                                                                                  &days_to_warn,
                                                                                  &days_after_expiration_until_lock,
                                                                                  res,
                                                                                  &error);

        if (!succeeded) {
                g_warning ("Failed to get password expiration policy for user: %s", error->message);
                goto out;
        }

        set_policy_number (&manager->priv->expiration_time, expiration_time);
        set_policy_number (&manager->priv->last_change_time, last_change_time);
        set_policy_number (&manager->priv->min_days_between_changes, min_days_between_changes);
        set_policy_number (&manager->priv->max_days_between_changes, max_days_between_changes);
        set_policy_number (&manager->priv->days_to_warn, days_to_warn);
        set_policy_number (&manager->priv->days_after_expiration_until_lock, days_after_expiration_until_lock);

        update_password_notification (manager);
out:
        gnome_settings_profile_end (NULL);
}

static void
on_got_accounts_user_proxy (GObject      *source_object,
                            GAsyncResult *res,
                            gpointer      user_data)
{
        GsdAccountManager *manager = user_data;
        g_autoptr(GError)  error = NULL;

        gnome_settings_profile_start (NULL);
        manager->priv->accounts_user_proxy = gsd_accounts_user_proxy_new_finish (res, &error);

        if (manager->priv->accounts_user_proxy != NULL) {
                gsd_accounts_user_call_get_password_expiration_policy (manager->priv->accounts_user_proxy,
                                                                       manager->priv->cancellable,
                                                                       (GAsyncReadyCallback)
                                                                       on_got_password_expiration_policy,
                                                                       manager);
        } else {
                g_warning ("Failed to get user proxy to accounts service: %s", error->message);
                goto out;
        }

out:
        gnome_settings_profile_end (NULL);
}

static void
on_got_user_object_path (GsdAccounts  *accounts_proxy,
                         GAsyncResult *res,
                         gpointer      user_data)
{
        GsdAccountManager *manager = user_data;
        g_autoptr(GError)  error = NULL;
        gboolean           succeeded;
        gchar             *object_path;
        GDBusConnection   *connection;

        gnome_settings_profile_start (NULL);

        succeeded = gsd_accounts_call_find_user_by_id_finish (accounts_proxy,
                                                              &object_path,
                                                              res,
                                                              &error);

        if (!succeeded) {
                g_warning ("Unable to find current user in accounts service: %s",
                           error->message);
                goto out;
        }

        connection = g_dbus_proxy_get_connection (G_DBUS_PROXY (accounts_proxy));
        gsd_accounts_user_proxy_new (connection,
                                     G_DBUS_PROXY_FLAGS_NONE,
                                     "org.freedesktop.Accounts",
                                     object_path,
                                     manager->priv->cancellable,
                                     (GAsyncReadyCallback)
                                     on_got_accounts_user_proxy,
                                     manager);

out:
        gnome_settings_profile_end (NULL);
}

static void
on_got_accounts_proxy (GObject      *source_object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
        GsdAccountManager *manager = user_data;
        g_autoptr(GError)  error = NULL;

        gnome_settings_profile_start (NULL);
        manager->priv->accounts_proxy = gsd_accounts_proxy_new_for_bus_finish (res, &error);

        if (manager->priv->accounts_proxy != NULL) {
                gsd_accounts_call_find_user_by_id (manager->priv->accounts_proxy,
                                                   getuid (),
                                                   manager->priv->cancellable,
                                                   (GAsyncReadyCallback)
                                                   on_got_user_object_path,
                                                   manager);
        } else {
                g_warning ("Failed to get proxy to accounts service: %s", error->message);
        }
        gnome_settings_profile_end (NULL);
}

gboolean
gsd_account_manager_start (GsdAccountManager  *manager,
                           GError            **error)
{
        g_debug ("Starting accounts manager");

        gnome_settings_profile_start (NULL);
        manager->priv->cancellable = g_cancellable_new ();
        gsd_accounts_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                        G_DBUS_PROXY_FLAGS_NONE,
                                        "org.freedesktop.Accounts",
                                        "/org/freedesktop/Accounts",
                                        manager->priv->cancellable,
                                        (GAsyncReadyCallback)
                                        on_got_accounts_proxy,
                                        manager);
        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_account_manager_stop (GsdAccountManager *manager)
{
        g_debug ("Stopping accounts manager");

        if (manager->priv->cancellable != NULL) {
                g_cancellable_cancel (manager->priv->cancellable);
                g_clear_object (&manager->priv->cancellable);
        }

        g_clear_object (&manager->priv->accounts_proxy);
        g_clear_object (&manager->priv->accounts_user_proxy);
        g_clear_object (&manager->priv->notification);
}

static void
gsd_account_manager_class_init (GsdAccountManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_account_manager_finalize;

        notify_init ("gnome-settings-daemon");

        g_type_class_add_private (klass, sizeof (GsdAccountManagerPrivate));
}

static void
gsd_account_manager_init (GsdAccountManager *manager)
{
        manager->priv = GSD_ACCOUNT_MANAGER_GET_PRIVATE (manager);
}

static void
gsd_account_manager_finalize (GObject *object)
{
        GsdAccountManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_ACCOUNT_MANAGER (object));

        manager = GSD_ACCOUNT_MANAGER (object);

        g_return_if_fail (manager->priv != NULL);

        gsd_account_manager_stop (manager);

        G_OBJECT_CLASS (gsd_account_manager_parent_class)->finalize (object);
}

GsdAccountManager *
gsd_account_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_ACCOUNT_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_ACCOUNT_MANAGER (manager_object);
}
