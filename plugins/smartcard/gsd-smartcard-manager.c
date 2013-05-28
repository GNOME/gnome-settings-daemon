/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2010,2011 Red Hat, Inc.
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

#include <glib.h>
#include <gio/gio.h>

#include "gnome-settings-plugin.h"
#include "gnome-settings-profile.h"
#include "gsd-smartcard-manager.h"
#include "gsd-smartcard-service.h"
#include "gsd-smartcard-enum-types.h"
#include "gsd-smartcard-utils.h"

#include <prerror.h>
#include <prinit.h>
#include <nss.h>
#include <pk11func.h>
#include <secmod.h>
#include <secerr.h>

#define GSD_SMARTCARD_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_SMARTCARD_MANAGER, GsdSmartcardManagerPrivate))

struct GsdSmartcardManagerPrivate
{
        guint start_idle_id;
        GsdSmartcardService *service;
        GCancellable *cancellable;

        GSettings *settings;

        guint32 nss_is_loaded : 1;
};

#define CONF_SCHEMA "org.gnome.settings-daemon.peripherals.smartcard"

static void     gsd_smartcard_manager_class_init  (GsdSmartcardManagerClass *klass);
static void     gsd_smartcard_manager_init        (GsdSmartcardManager      *self);
static void     gsd_smartcard_manager_finalize    (GObject                  *object);
G_DEFINE_TYPE (GsdSmartcardManager, gsd_smartcard_manager, G_TYPE_OBJECT)
G_DEFINE_QUARK (gsd-smartcard-manager-error, gsd_smartcard_manager_error)

static gpointer manager_object = NULL;

static void
gsd_smartcard_manager_class_init (GsdSmartcardManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_smartcard_manager_finalize;

        gsd_dbus_register_error_domain (GSD_SMARTCARD_MANAGER_ERROR,
                                        GSD_TYPE_SMARTCARD_MANAGER_ERROR);
        g_type_class_add_private (klass, sizeof (GsdSmartcardManagerPrivate));
}

static void
gsd_smartcard_manager_init (GsdSmartcardManager *self)
{
        self->priv = GSD_SMARTCARD_MANAGER_GET_PRIVATE (self);
}

static void
load_nss (GsdSmartcardManager *self)
{
        GsdSmartcardManagerPrivate *priv = self->priv;
        SECStatus status = SECSuccess;
        static const guint32 flags = NSS_INIT_READONLY
                                   | NSS_INIT_FORCEOPEN
                                   | NSS_INIT_NOROOTINIT
                                   | NSS_INIT_OPTIMIZESPACE
                                   | NSS_INIT_PK11RELOAD;

        g_debug ("attempting to load NSS database '%s'",
                 GSD_SMARTCARD_MANAGER_NSS_DB);

        PR_Init (PR_USER_THREAD, PR_PRIORITY_NORMAL, 0);

        status = NSS_Initialize (GSD_SMARTCARD_MANAGER_NSS_DB,
                                 "", "", SECMOD_DB, flags);

        if (status != SECSuccess) {
                gsize error_message_size;
                char *error_message;

                error_message_size = PR_GetErrorTextLength ();

                if (error_message_size == 0) {
                        g_debug ("NSS security system could not be initialized");
                } else {
                        error_message = g_alloca (error_message_size);
                        PR_GetErrorText (error_message);

                        g_debug ("NSS security system could not be initialized - %s",
                                 error_message);
                }
                priv->nss_is_loaded = FALSE;
                return;

        }

        g_debug ("NSS database '%s' loaded", GSD_SMARTCARD_MANAGER_NSS_DB);
        priv->nss_is_loaded = TRUE;
}

static void
unload_nss (GsdSmartcardManager *self)
{
        g_debug ("attempting to unload NSS security system with database '%s'",
                 GSD_SMARTCARD_MANAGER_NSS_DB);

        if (self->priv->nss_is_loaded) {
                NSS_Shutdown ();
                g_debug ("NSS database '%s' unloaded", GSD_SMARTCARD_MANAGER_NSS_DB);
        } else {
                g_debug ("NSS database '%s' already not loaded", GSD_SMARTCARD_MANAGER_NSS_DB);
        }
}

typedef struct
{
        SECMODModule *driver;
        GHashTable *smartcards;
} WatchSmartcardsOperation;

static void
on_watch_cancelled (GCancellable             *cancellable,
                    WatchSmartcardsOperation *operation)
{
        SECMOD_CancelWait (operation->driver);
}

static gboolean
watch_one_event_from_driver (GsdSmartcardManager       *self,
                             WatchSmartcardsOperation  *operation,
                             GCancellable              *cancellable,
                             GError                   **error)
{
        GsdSmartcardManagerPrivate *priv = self->priv;
        PK11SlotInfo *card, *old_card;
        CK_SLOT_ID slot_id;
        gulong handler_id;
        int old_slot_series = -1, slot_series;

        handler_id = g_cancellable_connect (cancellable,
                                            G_CALLBACK (on_watch_cancelled),
                                            operation,
                                            NULL);

        card = SECMOD_WaitForAnyTokenEvent (operation->driver, 0, PR_SecondsToInterval (1));

        g_cancellable_disconnect (cancellable, handler_id);

        if (card == NULL) {
                int error_code;

                error_code = PORT_GetError ();

                g_warning ("smartcard event function failed.");

                g_set_error (error,
                             GSD_SMARTCARD_MANAGER_ERROR,
                             GSD_SMARTCARD_MANAGER_ERROR_WITH_NSS,
                             "encountered unexpected error while "
                             "waiting for smartcard events (error %d)",
                             error_code);
                return FALSE;
        }

        slot_id = PK11_GetSlotID (card);
        slot_series = PK11_GetSlotSeries (card);

        old_card = g_hash_table_lookup (operation->smartcards, GINT_TO_POINTER ((int) slot_id));

        /* If there is a different card in the slot now than
         * there was before, then we need to emit a removed signal
         * for the old card
         */
        if (old_card != NULL) {
                old_slot_series = PK11_GetSlotSeries (old_card);

                if (old_slot_series != slot_series) {
                        /* Card registered with slot previously is
                         * different than this card, so update its
                         * exported state to track the implicit missed
                         * removal
                         */
                        gsd_smartcard_service_sync_token (priv->service, old_card, cancellable);
                }

                g_hash_table_remove (operation->smartcards, GINT_TO_POINTER ((int) slot_id));
        }

        if (PK11_IsPresent (card)) {
                g_debug ("Detected smartcard insertion event in slot %d",
                         (int) slot_id);

                g_hash_table_replace (operation->smartcards,
                                      GINT_TO_POINTER ((int) slot_id),
                                      PK11_ReferenceSlot (card));

                gsd_smartcard_service_sync_token (priv->service, card, cancellable);

        } else if (old_card == NULL) {
                /* If the just removed smartcard is not known to us then
                 * ignore the removal event. NSS sends a synthentic removal
                 * event for slots that are empty at startup
                 */
                g_debug ("Detected slot %d is empty in reader", (int) slot_id);
        } else {
                g_debug ("Detected smartcard removal event in slot %d", (int) slot_id);
                /* If the just removed smartcard is known to us then
                 * we need to update its exported state to reflect the
                 * removal
                 */
                if (old_slot_series == slot_series) {
                        gsd_smartcard_service_sync_token (priv->service, card, cancellable);
                }
        }

        PK11_FreeSlot (card);

        return TRUE;
}

static void
watch_smartcards_from_driver (GTask                    *task,
                              GsdSmartcardManager      *self,
                              WatchSmartcardsOperation *operation,
                              GCancellable             *cancellable)
{
        g_debug ("watching for smartcard events");
        while (!g_cancellable_is_cancelled (cancellable)) {
                gboolean watch_succeeded;
                GError *error = NULL;

                watch_succeeded = watch_one_event_from_driver (self, operation, cancellable, &error);

                if (!watch_succeeded) {
                        g_task_return_error (task, error);
                        break;
                }
        }
}

static void
destroy_watch_smartcards_operation (WatchSmartcardsOperation *operation)
{
        SECMOD_DestroyModule (operation->driver);
        g_hash_table_unref (operation->smartcards);
        g_free (operation);
}

static void
watch_smartcards_from_driver_async (GsdSmartcardManager *self,
                                    SECMODModule        *driver,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
        GTask *task;
        WatchSmartcardsOperation *operation;

        operation = g_new0 (WatchSmartcardsOperation, 1);
        operation->driver = SECMOD_ReferenceModule (driver);
        operation->smartcards = g_hash_table_new_full (NULL,
                                                       NULL,
                                                       NULL,
                                                       (GDestroyNotify)
                                                       PK11_FreeSlot);

        task = g_task_new (self, cancellable, callback, user_data);

        g_task_set_task_data (task,
                              operation,
                              (GDestroyNotify)
                              destroy_watch_smartcards_operation);

        g_task_run_in_thread (task, (GTaskThreadFunc) watch_smartcards_from_driver);
        g_object_unref (task);
}

typedef struct
{
  guint driver_registered : 1;
  guint smartcards_watched : 1;
} ActivateDriverOperation;

static void
try_to_complete_driver_activation (GTask *task)
{
        ActivateDriverOperation *operation;

        operation = g_task_get_task_data (task);

        if (!operation->driver_registered)
                return;

        if (!operation->smartcards_watched)
                return;

        g_task_return_boolean (task, TRUE);
}

static gboolean
register_driver_finish (GsdSmartcardManager  *self,
                        GAsyncResult         *result,
                        GError              **error)
{
        return gsd_finish_boolean_task (G_OBJECT (self), result, error);
}

static void
on_driver_registered (GsdSmartcardManager *self,
                      GAsyncResult        *result,
                      GTask               *task)
{
        ActivateDriverOperation *operation;
        GError *error = NULL;

        if (!register_driver_finish (self, result, &error)) {
                g_task_return_error (task, error);
                g_object_unref (task);
                return;
        }

        operation = g_task_get_task_data (task);
        operation->driver_registered = TRUE;

        try_to_complete_driver_activation (task);
}

static void
on_smartcards_from_driver_watched (GsdSmartcardManager *self,
                                   GAsyncResult        *result,
                                   GTask               *task)
{
        ActivateDriverOperation *operation;

        operation = g_task_get_task_data (task);
        operation->smartcards_watched = TRUE;

        try_to_complete_driver_activation (task);
}

typedef struct {
        SECMODModule  *driver;
        guint          idle_id;
        GError        *error;
} DriverRegistrationOperation;

static void
destroy_driver_registration_operation (DriverRegistrationOperation *operation)
{
        SECMOD_DestroyModule (operation->driver);
        g_free (operation);
}

static gboolean
on_task_thread_to_complete_driver_registration (GTask *task)
{
        DriverRegistrationOperation *operation;
        operation = g_task_get_task_data (task);

        if (operation->error != NULL)
                g_task_return_error (task, operation->error);
        else
                g_task_return_boolean (task, TRUE);

        return G_SOURCE_REMOVE;
}

static gboolean
on_main_thread_to_register_driver (GTask *task)
{
        GsdSmartcardManager *self;
        GsdSmartcardManagerPrivate *priv;
        DriverRegistrationOperation *operation;
        GSource *source;

        self = g_task_get_source_object (task);
        priv = self->priv;
        operation = g_task_get_task_data (task);

        gsd_smartcard_service_register_driver (priv->service,
                                               operation->driver);

        source = g_idle_source_new ();
        g_task_attach_source (task,
                              source,
                              (GSourceFunc)
                              on_task_thread_to_complete_driver_registration);
        g_source_unref (source);

        return G_SOURCE_REMOVE;
}

static void
register_driver (GsdSmartcardManager *self,
                 SECMODModule         *driver,
                 GCancellable         *cancellable,
                 GAsyncReadyCallback   callback,
                 gpointer              user_data)
{
        GTask *task;
        DriverRegistrationOperation *operation;

        task = g_task_new (self, cancellable, callback, user_data);
        operation = g_new0 (DriverRegistrationOperation, 1);
        operation->driver = SECMOD_ReferenceModule (driver);
        g_task_set_task_data (task,
                              operation,
                              (GDestroyNotify)
                              destroy_driver_registration_operation);

        operation->idle_id = g_idle_add ((GSourceFunc) on_main_thread_to_register_driver, task);
}

static void
activate_driver (GsdSmartcardManager *self,
                 SECMODModule        *driver,
                 GCancellable        *cancellable,
                 GAsyncReadyCallback  callback,
                 gpointer             user_data)
{
        ActivateDriverOperation *operation;
        GTask *task;

        g_debug ("Activating driver '%s'", driver->commonName);

        task = g_task_new (self, cancellable, callback, user_data);
        operation = g_new0 (ActivateDriverOperation, 1);
        g_task_set_task_data (task, operation, (GDestroyNotify) g_free);

        register_driver (self,
                         driver,
                         cancellable,
                         (GAsyncReadyCallback)
                         on_driver_registered,
                         task);
        watch_smartcards_from_driver_async (self,
                                            driver,
                                            cancellable,
                                            (GAsyncReadyCallback)
                                            on_smartcards_from_driver_watched,
                                            task);
}

typedef struct
{
  int pending_drivers_count;
  int activated_drivers_count;
} ActivateAllDriversOperation;

static gboolean
activate_driver_async_finish (GsdSmartcardManager  *self,
                              GAsyncResult         *result,
                              GError              **error)
{
        return gsd_finish_boolean_task (G_OBJECT (self), result, error);
}

static void
try_to_complete_all_drivers_activation (GTask *task)
{
        ActivateAllDriversOperation *operation;

        operation = g_task_get_task_data (task);

        if (operation->pending_drivers_count >= 0)
                return;

        if (operation->activated_drivers_count > 0)
                g_task_return_boolean (task, TRUE);
        else
                g_task_return_boolean (task, FALSE);

        g_object_unref (task);
}

static void
on_driver_activated (GsdSmartcardManager *self,
                     GAsyncResult        *result,
                     GTask               *task)
{
        GError *error = NULL;
        gboolean driver_activated;
        ActivateAllDriversOperation *operation;

        driver_activated = activate_driver_async_finish (self, result, &error);

        operation = g_task_get_task_data (task);

        if (driver_activated)
                operation->activated_drivers_count++;

        operation->pending_drivers_count--;

        try_to_complete_all_drivers_activation (task);
}

static void
activate_all_drivers_async (GsdSmartcardManager *self,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
        GTask *task;
        SECMODListLock *lock;
        SECMODModuleList *driver_list, *node;
        ActivateAllDriversOperation *operation;

        task = g_task_new (self, cancellable, callback, user_data);
        operation = g_new0 (ActivateAllDriversOperation, 1);
        g_task_set_task_data (task, operation, (GDestroyNotify) g_free);

        lock = SECMOD_GetDefaultModuleListLock ();

        g_assert (lock != NULL);

        SECMOD_GetReadLock (lock);
        driver_list = SECMOD_GetDefaultModuleList ();
        for (node = driver_list; node != NULL; node = node->next) {
                if (!SECMOD_HasRemovableSlots (node->module) ||
                    !node->module->loaded)
                        continue;
                operation->pending_drivers_count++;

                activate_driver (self,
                                 node->module,
                                 cancellable,
                                 (GAsyncReadyCallback)
                                 on_driver_activated,
                                 task);

        }
        SECMOD_ReleaseReadLock (lock);
}

static gboolean
activate_all_drivers_async_finish (GsdSmartcardManager  *self,
                                   GAsyncResult         *result,
                                   GError              **error)
{
        return gsd_finish_boolean_task (G_OBJECT (self), result, error);
}

static void
on_all_drivers_activated (GsdSmartcardManager *self,
                          GAsyncResult        *result,
                          GTask               *task)
{
        GError *error = NULL;
        gboolean driver_activated;

        driver_activated = activate_all_drivers_async_finish (self, result, &error);

        if (!driver_activated) {
                g_task_return_error (task, error);
                return;
        }

        g_task_return_boolean (task, TRUE);
}

static void
watch_smartcards (GTask               *task,
                  GsdSmartcardManager *self,
                  gpointer             data,
                  GCancellable        *cancellable)
{
        GMainContext *context;
        GMainLoop *loop;

        g_debug ("Getting list of suitable drivers");
        context = g_main_context_new ();
        g_main_context_push_thread_default (context);

        activate_all_drivers_async (self,
                                    cancellable,
                                    (GAsyncReadyCallback)
                                    on_all_drivers_activated,
                                    task);

        loop = g_main_loop_new (context, FALSE);
        g_main_loop_run (loop);
        g_main_loop_unref (loop);

        g_main_context_pop_thread_default (context);
        g_main_context_unref (context);
}

static void
watch_smartcards_async (GsdSmartcardManager *self,
                        GCancellable        *cancellable,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
        GTask *task;

        task = g_task_new (self, cancellable, callback, user_data);

        g_task_run_in_thread (task, (GTaskThreadFunc) watch_smartcards);

        g_object_unref (task);
}

static gboolean
watch_smartcards_async_finish (GsdSmartcardManager *self,
                               GAsyncResult        *result,
                               GError             **error)
{
        return gsd_finish_boolean_task (G_OBJECT (self), result, error);
}

static void
on_smartcards_watched (GsdSmartcardManager *self,
                       GAsyncResult        *result)
{
        GError *error = NULL;

        if (!watch_smartcards_async_finish (self, result, &error)) {
                g_debug ("Error watching smartcards: %s",
                         error->message);
                g_error_free (error);
        }
}

static void
on_service_created (GObject             *source_object,
                    GAsyncResult        *result,
                    GsdSmartcardManager *self)
{
        GsdSmartcardManagerPrivate *priv = self->priv;
        GsdSmartcardService *service;
        GError *error = NULL;

        service = gsd_smartcard_service_new_finish (result, &error);

        if (service == NULL) {
                g_warning("Couldn't create session bus service: %s",
                          error->message);
                g_error_free (error);
                return;
        }

        priv->service = service;

        watch_smartcards_async (self,
                                priv->cancellable,
                                (GAsyncReadyCallback)
                                on_smartcards_watched,
                                NULL);

}

static gboolean
gsd_smartcard_manager_idle_cb (GsdSmartcardManager *self)
{
        GsdSmartcardManagerPrivate *priv = self->priv;

        gnome_settings_profile_start (NULL);

        priv->cancellable = g_cancellable_new();
        priv->settings = g_settings_new (CONF_SCHEMA);

        load_nss (self);

        gsd_smartcard_service_new_async (self,
                                         priv->cancellable,
                                         (GAsyncReadyCallback)
                                         on_service_created,
                                         self);

        gnome_settings_profile_end (NULL);

        priv->start_idle_id = 0;
        return FALSE;
}

gboolean
gsd_smartcard_manager_start (GsdSmartcardManager  *self,
                             GError              **error)
{
        GsdSmartcardManagerPrivate *priv = self->priv;

        gnome_settings_profile_start (NULL);

        priv->start_idle_id = g_idle_add ((GSourceFunc) gsd_smartcard_manager_idle_cb, self);

        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_smartcard_manager_stop (GsdSmartcardManager *self)
{
        GsdSmartcardManagerPrivate *priv = self->priv;

        g_debug ("Stopping smartcard manager");

        unload_nss (self);

        g_clear_object (&priv->settings);
        g_clear_object (&priv->cancellable);
}

static void
gsd_smartcard_manager_finalize (GObject *object)
{
        GsdSmartcardManager *self;
        GsdSmartcardManagerPrivate *priv;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_SMARTCARD_MANAGER (object));

        self = GSD_SMARTCARD_MANAGER (object);
        priv = self->priv;

        g_return_if_fail (self->priv != NULL);

        if (priv->start_idle_id != 0)
                g_source_remove (priv->start_idle_id);

        G_OBJECT_CLASS (gsd_smartcard_manager_parent_class)->finalize (object);
}

GsdSmartcardManager *
gsd_smartcard_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_SMARTCARD_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_SMARTCARD_MANAGER (manager_object);
}
