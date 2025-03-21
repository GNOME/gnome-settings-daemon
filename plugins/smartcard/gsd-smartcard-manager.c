/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2010,2011 Red Hat, Inc.
 * Copyright (C) 2020 Canonical Ltd.
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

#include <glib.h>
#include <gio/gio.h>
#include <p11-kit/p11-kit.h>

#include "gnome-settings-profile.h"
#include "gnome-settings-bus.h"
#include "gsd-smartcard-manager.h"
#include "gsd-smartcard-service.h"
#include "gsd-smartcard-enum-types.h"
#include "gsd-smartcard-utils.h"

#define GSD_SESSION_MANAGER_LOGOUT_MODE_FORCE 2

struct _GsdSmartcardManager
{
        GsdApplication parent;

        guint start_idle_id;
        GsdSmartcardService *service;
        GList *smartcard_modules;
        GList *smartcards_watch_tasks;
        GCancellable *cancellable;

        GsdSessionManager *session_manager;
        GsdScreenSaver *screen_saver;

        GSettings *settings;
};

#define CONF_SCHEMA "org.gnome.settings-daemon.peripherals.smartcard"
#define KEY_REMOVE_ACTION "removal-action"

static void     gsd_smartcard_manager_class_init  (GsdSmartcardManagerClass *klass);
static void     gsd_smartcard_manager_init        (GsdSmartcardManager      *self);
static void     gsd_smartcard_manager_startup     (GApplication             *app);
static void     gsd_smartcard_manager_shutdown     (GApplication             *app);
static void     lock_screen                       (GsdSmartcardManager *self);
static void     log_out                           (GsdSmartcardManager *self);
static void     on_smartcards_from_module_watched (GsdSmartcardManager *self,
                                                   GAsyncResult        *result,
                                                   gpointer             user_data);
G_DEFINE_TYPE (GsdSmartcardManager, gsd_smartcard_manager, GSD_TYPE_APPLICATION)
G_DEFINE_QUARK (gsd-smartcard-manager-error, gsd_smartcard_manager_error)
G_LOCK_DEFINE_STATIC (gsd_smartcards_watch_tasks);

static void
gsd_smartcard_manager_class_init (GsdSmartcardManagerClass *klass)
{
        GApplicationClass *application_class = G_APPLICATION_CLASS (klass);

        application_class->startup = gsd_smartcard_manager_startup;
        application_class->shutdown = gsd_smartcard_manager_shutdown;

        gsd_smartcard_utils_register_error_domain (GSD_SMARTCARD_MANAGER_ERROR,
                                                   GSD_TYPE_SMARTCARD_MANAGER_ERROR);
}

static void
gsd_smartcard_manager_init (GsdSmartcardManager *self)
{
}

typedef struct
{
        GckModule *module;
        GHashTable *smartcards;
        int number_of_consecutive_errors;
} WatchSmartcardsOperation;

static void
on_watch_cancelled (GCancellable             *cancellable,
                    WatchSmartcardsOperation *operation)
{
        CK_FUNCTION_LIST_PTR p11_module;

        p11_module = gck_module_get_functions (operation->module);

        /* This will make C_WaitForSlotEvent return CKR_CRYPTOKI_NOT_INITIALIZED */
        p11_module->C_Finalize (NULL);

        /* And we initialize it again, even though it could not be really needed */
        p11_module->C_Initialize (NULL);
}

static GckSlot *
get_module_slot_by_handle (GckModule *module,
                           gulong     handle,
                           gboolean   with_token)
{
        g_autolist(GckSlot) slots = gck_module_get_slots (module, with_token);
        GList *l;

        for (l = slots; l; l = l->next) {
                GckSlot *slot = l->data;

                if (gck_slot_get_handle (slot) == handle)
                        return g_object_ref (slot);
        }

        return NULL;
}

static GckSlot *
wait_for_any_slot_event (GckModule  *module,
                         gboolean   *wait_blocks,
                         GError    **error)
{
        GckSlot *slot;
        CK_FUNCTION_LIST_PTR p11_module;
        CK_SLOT_ID slot_id;
        CK_RV ret;

        p11_module = gck_module_get_functions (module);

        /* We first try to use the blocking version of the call, in case it
         * is not supported, we fallback in the non-blocking version as
         * historically not all the p11-kit modules used supported it.
         */
        *wait_blocks = TRUE;
        ret = p11_module->C_WaitForSlotEvent (0, &slot_id, NULL);
        if (ret == CKR_FUNCTION_NOT_SUPPORTED) {
                *wait_blocks = FALSE;
                ret = p11_module->C_WaitForSlotEvent (CKF_DONT_BLOCK, &slot_id, NULL);
        }

        switch (ret) {
        case CKR_NO_EVENT:
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_AGAIN,
                             "Got no event, ignoring...");
                return NULL;
        case CKR_FUNCTION_NOT_SUPPORTED:
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                             "Device does not support waiting for slot events");
                return NULL;
        case CKR_CRYPTOKI_NOT_INITIALIZED:
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                             "Slot wait event cancelled");
                return NULL;
        default:
                if (ret != CKR_OK) {
                        g_set_error (error, GSD_SMARTCARD_MANAGER_ERROR,
                                GSD_SMARTCARD_MANAGER_ERROR_WITH_P11KIT,
                                "Failed to wait for slot event, error: %lx.",
                                ret);
                        return NULL;
                }
                break;
        }

        slot = get_module_slot_by_handle (module, slot_id, FALSE);

        if (!slot) {
                g_autofree char *module_name = NULL;

                module_name = p11_kit_module_get_name (gck_module_get_functions (module));
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                             "Slot with ID %lu not found in module %s",
                             slot_id, module_name);
        }

        return slot;
}

static gboolean
token_info_equals (GckTokenInfo *a, GckTokenInfo *b)
{
        if ((a && !b) || (!a && b))
                return FALSE;
        if (a->total_private_memory != b->total_private_memory)
                return FALSE;
        if (a->total_public_memory != b->total_public_memory)
                return FALSE;
        if (a->hardware_version_major != b->hardware_version_major)
                return FALSE;
        if (a->hardware_version_minor != b->hardware_version_minor)
                return FALSE;
        if (a->firmware_version_major != b->firmware_version_major)
                return FALSE;
        if (a->firmware_version_minor != b->firmware_version_minor)
                return FALSE;
        if (g_strcmp0 (a->serial_number, b->serial_number) != 0)
                return FALSE;
        if (g_strcmp0 (a->manufacturer_id, b->manufacturer_id) != 0)
                return FALSE;
        if (g_strcmp0 (a->model, b->model) != 0)
                return FALSE;
        if (g_strcmp0 (a->label, b->label) != 0)
                return FALSE;

        return TRUE;
}

static GckSlot *
get_changed_slot (WatchSmartcardsOperation *operation)
{
        g_autolist(GckSlot) slots_with_token = NULL;
        GHashTableIter iter;
        gpointer key, value;
        GList *l;

        slots_with_token = gck_module_get_slots (operation->module, TRUE);

        g_hash_table_iter_init (&iter, operation->smartcards);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                GckSlot *slot = key;
                GckTokenInfo *old_token = value;
                g_autoptr(GckTokenInfo) current_token = NULL;

                if (!g_list_find_custom (slots_with_token, slot, (GCompareFunc) gck_slot_equal)) {
                        /* Saved slot has not a token anymore */
                        return g_object_ref (slot);
                }

                current_token = gck_slot_get_token_info (slot);
                if (!token_info_equals (current_token, old_token)) {
                        return g_object_ref (slot);
                }
        }

        /* At this point all the saved tokens match the ones in device.
         * Now we need to check if there's a token that is not saved */
        for (l = slots_with_token; l; l = l->next) {
                GckSlot *slot = l->data;

                if (!g_hash_table_contains (operation->smartcards, slot))
                        return g_object_ref (slot);
        }

        return NULL;
}

static gboolean
watch_one_event_from_module (GsdSmartcardManager       *self,
                             WatchSmartcardsOperation  *operation,
                             GCancellable              *cancellable,
                             GError                   **error)
{
        g_autoptr(GError) wait_error = NULL;
        g_autoptr(GckSlot) slot = NULL;
        g_autoptr(GckTokenInfo) token = NULL;
        GckTokenInfo *old_token;
        gulong return_sleep = 0;
        gulong handler_id;
        gboolean token_changed;
        gboolean wait_blocks;

        wait_blocks = FALSE;
        handler_id = g_cancellable_connect (cancellable,
                                            G_CALLBACK (on_watch_cancelled),
                                            operation,
                                            NULL);

        if (handler_id != 0) {
                slot = wait_for_any_slot_event (operation->module, &wait_blocks,
                                                &wait_error);
        }

        g_cancellable_disconnect (cancellable, handler_id);

        if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
                g_warning ("smartcard event function cancelled");
                return FALSE;
        }

        if (g_error_matches (wait_error, G_IO_ERROR, G_IO_ERROR_AGAIN)) {
                if (!wait_blocks) {
                        g_usleep (1 * G_USEC_PER_SEC);
                }
                return TRUE;
        } else if (g_error_matches (wait_error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED)) {
                slot = get_changed_slot (operation);
                if (slot) {
                        return_sleep = wait_blocks ? 0 : 1 * G_USEC_PER_SEC;
                        g_clear_error (&wait_error);
                } else {
                        if (!wait_blocks) {
                                g_usleep (1 * G_USEC_PER_SEC);
                        }
                        return TRUE;
                }
        }

        if (wait_error) {
                operation->number_of_consecutive_errors++;
                if (operation->number_of_consecutive_errors > 10) {
                     g_warning ("Got %d consecutive smartcard errors, so giving up.",
                                operation->number_of_consecutive_errors);

                     g_set_error (error, wait_error->domain, wait_error->code,
                                  "encountered unexpected error while "
                                  "waiting for smartcard event: %s",
                                  wait_error->message);
                     return FALSE;
                }

                g_warning ("Got potentially spurious smartcard event error: %s",
                           wait_error->message);

                if (!wait_blocks) {
                        g_usleep (1 * G_USEC_PER_SEC);
                }
                return TRUE;
        }
        operation->number_of_consecutive_errors = 0;

        g_assert (slot);
        if (gck_slot_has_flags (slot, CKF_TOKEN_PRESENT)) {
                token = gck_slot_get_token_info (slot);
        }
        old_token = g_hash_table_lookup (operation->smartcards, slot);
        token_changed = TRUE;


        /* If there is a different card in the slot now than
         * there was before, then we need to emit a removed signal
         * for the old card
         */
        if (old_token != NULL) {
                if (token) {
                        token_changed = !token_info_equals (token, old_token);
                }

                if (token_changed) {
                        /* Card registered with slot previously is
                         * different than this card, so update its
                         * exported state to track the implicit missed
                         * removal
                         */
                        gsd_smartcard_service_sync_token (self->service, slot, cancellable);

                        g_hash_table_remove (operation->smartcards, slot);
                }
        }

        if (token) {
                if (token_changed) {
                        g_debug ("Detected smartcard insertion event in slot %lu",
                                 gck_slot_get_handle (slot));

                        g_hash_table_replace (operation->smartcards,
                                              g_object_ref (slot),
                                              g_steal_pointer (&token));

                        gsd_smartcard_service_sync_token (self->service, slot,
                                                          cancellable);
                }
        } else if (old_token == NULL) {
                /* If the just removed smartcard is not known to us then
                 * ignore the removal event. NSS sends a synthentic removal
                 * event for slots that are empty at startup
                 */
                g_debug ("Detected slot %lu is empty in reader",
                          gck_slot_get_handle (slot));
        } else {
                g_debug ("Detected smartcard removal event in slot %lu",
                         gck_slot_get_handle (slot));

                /* If the just removed smartcard is known to us then
                 * we need to update its exported state to reflect the
                 * removal
                 */
                if (!token_changed)
                        gsd_smartcard_service_sync_token (self->service, slot, cancellable);
        }

        g_usleep (return_sleep);

        return TRUE;
}

static void
watch_smartcards_from_module (GTask                    *task,
                              GsdSmartcardManager      *self,
                              WatchSmartcardsOperation *operation,
                              GCancellable             *cancellable)
{
        g_debug ("watching for smartcard events");
        while (!g_cancellable_is_cancelled (cancellable)) {
                gboolean watch_succeeded;
                g_autoptr(GError) error = NULL;

                watch_succeeded = watch_one_event_from_module (self, operation, cancellable, &error);

                if (g_task_return_error_if_cancelled (task)) {
                        break;
                }

                if (!watch_succeeded) {
                        g_task_return_error (task, g_steal_pointer (&error));
                        break;
                }
        }
}

static void
destroy_watch_smartcards_operation (WatchSmartcardsOperation *operation)
{
        g_clear_object (&operation->module);
        g_hash_table_unref (operation->smartcards);
        g_free (operation);
}

static void
on_smartcards_watch_task_destroyed (GsdSmartcardManager *self,
                                    GTask               *freed_task)
{
        g_autoptr(GMutexLocker) locked = NULL;

        locked = g_mutex_locker_new (&G_LOCK_NAME (gsd_smartcards_watch_tasks));
        self->smartcards_watch_tasks = g_list_remove (self->smartcards_watch_tasks,
                                                      freed_task);
}

static void
sync_initial_tokens_from_driver (GsdSmartcardManager *self,
                                 GckModule           *module,
                                 GHashTable          *smartcards,
                                 GCancellable        *cancellable)
{
        GList *l;
        g_autolist(GckSlot) full_slots = NULL;

        full_slots = gck_module_get_slots (module, TRUE);

        for (l = full_slots; l; l = l->next) {
                GckSlot *slot = l->data;
                GckTokenInfo *token_info;

                if (!gck_slot_has_flags (slot, CKF_TOKEN_PRESENT)) {
                        CK_FUNCTION_LIST_PTR p11k_module = gck_module_get_functions (module);
                        g_autofree char *module_name = p11_kit_module_get_name (p11k_module);

                        g_warning ("Module %s returned slot with no tokens",
                                   module_name);
                        continue;
                }

                /* gck_slot_get_token_info() may return an error in case the
                 * underlying p11k module call to C_GetTokenInfo() fails.
                 * The gck API doesn't expose that (if not with a warning), but
                 * it may still return a NULL token info (for example when an
                 * inserted token is not recognized).
                 * So handle this case gracefully to prevent us to crash.
                 */
                token_info = gck_slot_get_token_info (slot);
                if (!token_info) {
                        CK_FUNCTION_LIST_PTR p11k_module = gck_module_get_functions (module);
                        g_autofree char *module_name = p11_kit_module_get_name (p11k_module);

                        g_warning ("Module %s returned slot %lu has no valid token",
                                   module_name, gck_slot_get_handle (slot));
                        continue;
                }

                g_debug ("Detected smartcard '%s' in slot %lu at start up",
                         token_info->label, gck_slot_get_handle (slot));

                g_hash_table_replace (smartcards, g_object_ref (slot), token_info);

                gsd_smartcard_service_sync_token (self->service, slot, cancellable);
        }
}

static void
watch_smartcards_from_module_async (GsdSmartcardManager *self,
                                    GckModule           *module,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
        GTask *task;
        WatchSmartcardsOperation *operation;

        operation = g_new0 (WatchSmartcardsOperation, 1);
        operation->module = g_object_ref (module);
        operation->smartcards = g_hash_table_new_full ((GHashFunc) gck_slot_hash,
                                                       (GEqualFunc) gck_slot_equal,
                                                       g_object_unref,
                                                       (GDestroyNotify) gck_token_info_free);

        task = g_task_new (self, cancellable, callback, user_data);

        g_task_set_task_data (task,
                              operation,
                              (GDestroyNotify) destroy_watch_smartcards_operation);

        G_LOCK (gsd_smartcards_watch_tasks);
        self->smartcards_watch_tasks = g_list_prepend (self->smartcards_watch_tasks,
                                                       task);
        g_object_weak_ref (G_OBJECT (task),
                           (GWeakNotify) on_smartcards_watch_task_destroyed,
                           self);
        G_UNLOCK (gsd_smartcards_watch_tasks);

        sync_initial_tokens_from_driver (self, module, operation->smartcards, cancellable);

        g_task_run_in_thread (task, (GTaskThreadFunc) watch_smartcards_from_module);
}

static void
on_smartcards_from_module_watched (GsdSmartcardManager *self,
                                   GAsyncResult        *result,
                                   gpointer             user_data)
{
        g_autoptr(GError) error = NULL;

        if (!g_task_propagate_boolean (G_TASK (result), &error)) {
                g_debug ("Done watching smartcards from module: %s", error->message);
                return;
        }
        g_debug ("Done watching smartcards from module");
}

static gboolean
module_should_be_watched (GckModule *module)
{
        g_autolist(GckSlot) slots = NULL;
        GList *l;

        slots = gck_module_get_slots (module, FALSE);

        if (slots == NULL) {
                CK_FUNCTION_LIST_PTR p11_module;
                g_autofree char *module_name = NULL;

                p11_module = gck_module_get_functions (module);
                module_name = p11_kit_module_get_name (p11_module);

                /* No slot is currently available, so we can't make assumptions
                 * whether the module supports or not removable devices.
                 * So let's be conservative here and let's just assume that the
                 * module does support removable devices, so that we will monitor
                 * it for changes.
                 */
                g_debug ("No slot found for module %s, let's assume it supports "
                         "removable devices", module_name);
                return TRUE;
        }

        for (l = slots; l; l = l->next) {
                GckSlot *slot = l->data;

                if (gck_slot_has_flags (slot, CKF_REMOVABLE_DEVICE))
                        return TRUE;
        }

        return FALSE;
}

static void
on_modules_initialized (GObject      *source_object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
        g_autoptr(GTask) task = NULL;
        g_autoptr(GError) error = NULL;
        g_autolist(GckModule) modules = NULL;
        g_autoptr(GckSlot) login_token = NULL;
        GsdSmartcardManager *self;
        GList *l;

        task = g_steal_pointer (&user_data);
        self = g_task_get_source_object (task);
        modules = gck_modules_initialize_registered_finish (result, &error);

        if (error) {
                g_task_return_error (task, g_steal_pointer (&error));
                return;
        }

        for (l = modules; l; l = l->next) {
                GckModule *module = l->data;
                CK_FUNCTION_LIST_PTR p11_module;
                g_autofree char *module_name = NULL;
                gboolean should_watch;

                p11_module = gck_module_get_functions (module);
                module_name = p11_kit_module_get_name (p11_module);
                should_watch = module_should_be_watched (module);

                g_debug ("Found p11-kit module %s (watched: %d)", module_name,
                         should_watch);

                if (!should_watch)
                        continue;

                self->smartcard_modules = g_list_prepend (self->smartcard_modules,
                                                          g_object_ref (module));

                gsd_smartcard_service_register_driver (self->service, module);
                watch_smartcards_from_module_async (self,
                                                    module,
                                                    self->cancellable,
                                                    (GAsyncReadyCallback) on_smartcards_from_module_watched,
                                                    NULL);
        }

        if (!self->smartcard_modules) {
                g_task_return_new_error (task, GSD_SMARTCARD_MANAGER_ERROR,
                                         GSD_SMARTCARD_MANAGER_ERROR_NO_DRIVERS,
                                         "No smartcard exist to be activated.");
                return;
        }

        login_token = gsd_smartcard_manager_get_login_token (self);

        if (login_token || gsd_smartcard_utils_get_login_token_name () != NULL) {
                if (!login_token ||
                    !gck_slot_has_flags (login_token, CKF_TOKEN_PRESENT))
                        gsd_smartcard_manager_do_remove_action (self);
        }

        g_task_return_boolean (task, TRUE);
}

static void
watch_smartcards_async (GsdSmartcardManager *self,
                        GCancellable        *cancellable,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
        GTask *task;

        task = g_task_new (self, cancellable, callback, user_data);

        gck_modules_initialize_registered_async (self->cancellable,
                                                 (GAsyncReadyCallback) on_modules_initialized,
                                                 task);
}

static gboolean
watch_smartcards_async_finish (GsdSmartcardManager  *self,
                               GAsyncResult         *result,
                               GError              **error)
{
        return g_task_propagate_boolean (G_TASK (result), error);
}

static void
on_smartcards_watched (GsdSmartcardManager *self,
                       GAsyncResult        *result)
{
        g_autoptr(GError) error = NULL;

        if (!watch_smartcards_async_finish (self, result, &error)) {
                g_debug ("Error watching smartcards: %s", error->message);
        }
}

static void
on_service_created (GObject             *source_object,
                    GAsyncResult        *result,
                    GsdSmartcardManager *self)
{
        GsdSmartcardService *service;
        g_autoptr(GError) error = NULL;

        service = gsd_smartcard_service_new_finish (result, &error);

        if (service == NULL) {
                g_warning("Couldn't create session bus service: %s", error->message);
                return;
        }

        self->service = service;

        g_debug("Service created, getting modules...");

        watch_smartcards_async (self,
                                self->cancellable,
                                (GAsyncReadyCallback) on_smartcards_watched,
                                NULL);
}

static gboolean
gsd_smartcard_manager_idle_cb (GsdSmartcardManager *self)
{
        gnome_settings_profile_start (NULL);

        self->cancellable = g_cancellable_new();
        self->settings = g_settings_new (CONF_SCHEMA);

        gsd_smartcard_service_new_async (self,
                                         self->cancellable,
                                         (GAsyncReadyCallback) on_service_created,
                                         self);

        gnome_settings_profile_end (NULL);

        self->start_idle_id = 0;

        return G_SOURCE_REMOVE;
}

static void
gsd_smartcard_manager_startup (GApplication *app)
{
        GsdSmartcardManager *self = GSD_SMARTCARD_MANAGER (app);

        gnome_settings_profile_start (NULL);

        self->start_idle_id = g_idle_add ((GSourceFunc) gsd_smartcard_manager_idle_cb, self);
        g_source_set_name_by_id (self->start_idle_id, "[gnome-settings-daemon] gsd_smartcard_manager_idle_cb");

        G_APPLICATION_CLASS (gsd_smartcard_manager_parent_class)->startup (app);

        gnome_settings_profile_end (NULL);
}

static void
gsd_smartcard_manager_shutdown (GApplication *app)
{
        GsdSmartcardManager *self = GSD_SMARTCARD_MANAGER (app);

        g_debug ("Stopping smartcard manager");

        g_cancellable_cancel (self->cancellable);

        g_list_free_full (g_steal_pointer (&self->smartcard_modules), g_object_unref);
        g_clear_object (&self->settings);
        g_clear_object (&self->cancellable);
        g_clear_object (&self->session_manager);
        g_clear_object (&self->screen_saver);
        g_clear_handle_id (&self->start_idle_id, g_source_remove);

        G_APPLICATION_CLASS (gsd_smartcard_manager_parent_class)->shutdown (app);
}

static void
on_screen_locked (GsdScreenSaver      *screen_saver,
                  GAsyncResult        *result,
                  GsdSmartcardManager *self)
{
        g_autoptr(GError) error = NULL;
        gboolean is_locked;

        is_locked = gsd_screen_saver_call_lock_finish (screen_saver, result, &error);

        if (!is_locked) {
                g_warning ("Couldn't lock screen: %s", error->message);
                return;
        }
}

static void
lock_screen (GsdSmartcardManager *self)
{
        if (self->screen_saver == NULL)
                self->screen_saver = gnome_settings_bus_get_screen_saver_proxy ();

        gsd_screen_saver_call_lock (self->screen_saver,
                                    self->cancellable,
                                    (GAsyncReadyCallback) on_screen_locked,
                                    self);
}

static void
on_logged_out (GsdSessionManager   *session_manager,
               GAsyncResult        *result,
               GsdSmartcardManager *self)
{
        g_autoptr(GError) error = NULL;
        gboolean is_logged_out;

        is_logged_out = gsd_session_manager_call_logout_finish (session_manager, result, &error);

        if (!is_logged_out) {
                g_warning ("Couldn't log out: %s", error->message);
                return;
        }
}

static void
log_out (GsdSmartcardManager *self)
{
        if (self->session_manager == NULL)
                self->session_manager = gnome_settings_bus_get_session_proxy ();

        gsd_session_manager_call_logout (self->session_manager,
                                         GSD_SESSION_MANAGER_LOGOUT_MODE_FORCE,
                                         self->cancellable,
                                         (GAsyncReadyCallback) on_logged_out,
                                         self);
}

void
gsd_smartcard_manager_do_remove_action (GsdSmartcardManager *self)
{
        char *remove_action;

        remove_action = g_settings_get_string (self->settings, KEY_REMOVE_ACTION);
        g_debug("Do remove action %s", remove_action);

        if (strcmp (remove_action, "lock-screen") == 0)
                lock_screen (self);
        else if (strcmp (remove_action, "force-logout") == 0)
                log_out (self);
}

static GckSlot *
get_login_token_for_operation (GsdSmartcardManager      *self,
                               WatchSmartcardsOperation *operation)
{
        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init (&iter, operation->smartcards);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                GckSlot *card_slot = key;
                GckTokenInfo *token_info = value;
                const char *token_name = token_info->label;

                if (g_strcmp0 (gsd_smartcard_utils_get_login_token_name (), token_name) == 0)
                        return g_object_ref (card_slot);
        }

        return NULL;
}

GckSlot *
gsd_smartcard_manager_get_login_token (GsdSmartcardManager *self)
{
        g_autoptr(GMutexLocker) locked = NULL;
        GckSlot *card_slot = NULL;
        GList *node;

        locked = g_mutex_locker_new (&G_LOCK_NAME (gsd_smartcards_watch_tasks));
        node = self->smartcards_watch_tasks;
        while (node != NULL) {
                GTask *task = node->data;
                WatchSmartcardsOperation *operation = g_task_get_task_data (task);

                card_slot = get_login_token_for_operation (self, operation);

                if (card_slot != NULL)
                        break;

                node = node->next;
        }

        return card_slot;
}

static GList *
get_inserted_tokens_for_operation (GsdSmartcardManager      *self,
                                   WatchSmartcardsOperation *operation)
{
        GList *inserted_tokens = NULL;
        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init (&iter, operation->smartcards);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                GckSlot *card_slot = key;

                if (gck_slot_has_flags (card_slot, CKF_TOKEN_PRESENT))
                        inserted_tokens = g_list_prepend (inserted_tokens, g_object_ref (card_slot));
        }

        return inserted_tokens;
}

GList *
gsd_smartcard_manager_get_inserted_tokens (GsdSmartcardManager *self,
                                           gsize               *num_tokens)
{
        g_autoptr(GMutexLocker) locked = NULL;
        GList *inserted_tokens = NULL, *node;

        locked = g_mutex_locker_new (&G_LOCK_NAME (gsd_smartcards_watch_tasks));
        for (node = self->smartcards_watch_tasks; node != NULL; node = node->next) {
                GTask *task = node->data;
                WatchSmartcardsOperation *operation = g_task_get_task_data (task);
                GList *operation_inserted_tokens;

                operation_inserted_tokens = get_inserted_tokens_for_operation (self, operation);

                inserted_tokens = g_list_concat (inserted_tokens, operation_inserted_tokens);
        }

        if (num_tokens != NULL)
                *num_tokens = g_list_length (inserted_tokens);

        return inserted_tokens;
}
