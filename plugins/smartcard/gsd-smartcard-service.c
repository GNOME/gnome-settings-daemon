/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Red Hat, Inc.
 * Copyright (C) 2020 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Ray Strode
 */

#include "config.h"

#include "gsd-smartcard-service.h"
#include "org.gnome.SettingsDaemon.Smartcard.h"
#include "gsd-smartcard-manager.h"
#include "gsd-smartcard-enum-types.h"
#include "gsd-smartcard-utils.h"

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <p11-kit/p11-kit.h>

struct _GsdSmartcardService
{
        GsdSmartcardServiceManagerSkeleton parent;

        GDBusConnection            *bus_connection;
        GDBusObjectManagerServer   *object_manager_server;
        GsdSmartcardManager        *smartcard_manager;
        GCancellable               *cancellable;
        GHashTable                 *tokens;

        gboolean                    login_token_bound;
        guint name_id;
};

#define GSD_DBUS_NAME "org.gnome.SettingsDaemon"
#define GSD_DBUS_PATH "/org/gnome/SettingsDaemon"
#define GSD_DBUS_BASE_INTERFACE "org.gnome.SettingsDaemon"

#define GSD_SMARTCARD_DBUS_NAME GSD_DBUS_NAME ".Smartcard"
#define GSD_SMARTCARD_DBUS_PATH GSD_DBUS_PATH "/Smartcard"
#define GSD_SMARTCARD_MANAGER_DBUS_PATH GSD_SMARTCARD_DBUS_PATH "/Manager"
#define GSD_SMARTCARD_MANAGER_DRIVERS_DBUS_PATH GSD_SMARTCARD_MANAGER_DBUS_PATH "/Drivers"
#define GSD_SMARTCARD_MANAGER_TOKENS_DBUS_PATH  GSD_SMARTCARD_MANAGER_DBUS_PATH "/Tokens"

enum {
        PROP_0,
        PROP_MANAGER,
        PROP_BUS_CONNECTION
};

static void gsd_smartcard_service_set_property (GObject *object,
                                                guint property_id,
                                                const GValue *value,
                                                GParamSpec *param_spec);
static void gsd_smartcard_service_get_property (GObject *object,
                                                guint property_id,
                                                GValue *value,
                                                GParamSpec *param_spec);
static void async_initable_interface_init (GAsyncInitableIface *interface);
static void smartcard_service_manager_interface_init (GsdSmartcardServiceManagerIface *interface);

G_LOCK_DEFINE_STATIC (gsd_smartcard_tokens);

G_DEFINE_TYPE_WITH_CODE (GsdSmartcardService,
                         gsd_smartcard_service,
                         GSD_SMARTCARD_SERVICE_TYPE_MANAGER_SKELETON,
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                                                async_initable_interface_init)
                         G_IMPLEMENT_INTERFACE (GSD_SMARTCARD_SERVICE_TYPE_MANAGER,
                                                smartcard_service_manager_interface_init));

static void
set_bus_connection (GsdSmartcardService  *self,
                    GDBusConnection      *connection)
{
        if (self->bus_connection != connection) {
                g_clear_object (&self->bus_connection);
                self->bus_connection = g_object_ref (connection);
                g_object_notify (G_OBJECT (self), "bus-connection");
        }
}

static void
register_object_manager (GsdSmartcardService *self)
{
        g_autoptr(GsdSmartcardServiceObjectSkeleton) object = NULL;

        self->object_manager_server = g_dbus_object_manager_server_new (GSD_SMARTCARD_DBUS_PATH);

        object = gsd_smartcard_service_object_skeleton_new (GSD_SMARTCARD_MANAGER_DBUS_PATH);
        gsd_smartcard_service_object_skeleton_set_manager (object,
                                                           GSD_SMARTCARD_SERVICE_MANAGER (self));

        g_dbus_object_manager_server_export (self->object_manager_server,
                                             G_DBUS_OBJECT_SKELETON (object));
        g_dbus_object_manager_server_set_connection (self->object_manager_server,
                                                     self->bus_connection);
}

static const char *
get_login_token_object_path (GsdSmartcardService *self)
{
        return GSD_SMARTCARD_MANAGER_TOKENS_DBUS_PATH "/login_token";
}

static void
register_login_token_alias (GsdSmartcardService *self)
{
        g_autoptr(GDBusObjectSkeleton) object = NULL;
        g_autoptr(GDBusInterfaceSkeleton) interface = NULL;
        const char *object_path;
        const char *token_name;

        token_name = gsd_smartcard_utils_get_login_token_name ();

        if (token_name == NULL)
                return;

        object_path = get_login_token_object_path (self);
        object = G_DBUS_OBJECT_SKELETON (gsd_smartcard_service_object_skeleton_new (object_path));
        interface = G_DBUS_INTERFACE_SKELETON (gsd_smartcard_service_token_skeleton_new ());

        g_dbus_object_skeleton_add_interface (object, interface);

        g_object_set (G_OBJECT (interface),
                      "name", token_name,
                      "used-to-login", TRUE,
                      "is-inserted", FALSE,
                      NULL);

        g_dbus_object_manager_server_export (self->object_manager_server,
                                             object);

        G_LOCK (gsd_smartcard_tokens);
        g_hash_table_insert (self->tokens, g_strdup (object_path), interface);
        G_UNLOCK (gsd_smartcard_tokens);
}

static void
on_bus_gotten (GObject      *source_object,
               GAsyncResult *result,
               gpointer      user_data)
{
        g_autoptr(GTask) task = g_steal_pointer (&user_data);
        GsdSmartcardService *self;
        GDBusConnection *connection;
        GError *error = NULL;

        connection = g_bus_get_finish (result, &error);
        if (connection == NULL) {
                g_task_return_error (task, error);
                return;
        }

        g_debug ("taking name %s on session bus", GSD_SMARTCARD_DBUS_NAME);

        self = g_task_get_source_object (task);

        set_bus_connection (self, connection);

        register_object_manager (self);
        self->name_id = g_bus_own_name_on_connection (connection,
                                                      GSD_SMARTCARD_DBUS_NAME,
                                                      G_BUS_NAME_OWNER_FLAGS_NONE,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL);

        /* In case the login token is removed at start up, register an
         * an alias interface that's always around
         */
        register_login_token_alias (self);
        g_task_return_boolean (task, TRUE);
}

static gboolean
gsd_smartcard_service_initable_init_finish (GAsyncInitable  *initable,
                                            GAsyncResult    *result,
                                            GError         **error)
{
        GTask *task;

        task = G_TASK (result);

        return g_task_propagate_boolean (task, error);
}

static void
gsd_smartcard_service_initable_init_async (GAsyncInitable      *initable,
                                           int                  io_priority,
                                           GCancellable        *cancellable,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data)
{
        GsdSmartcardService *self = GSD_SMARTCARD_SERVICE (initable);
        GTask *task;

        task = g_task_new (G_OBJECT (self), cancellable, callback, user_data);
        g_task_set_priority (task, io_priority);

        g_bus_get (G_BUS_TYPE_SESSION, cancellable, (GAsyncReadyCallback) on_bus_gotten, task);
}

static void
async_initable_interface_init (GAsyncInitableIface *interface)
{
        interface->init_async = gsd_smartcard_service_initable_init_async;
        interface->init_finish = gsd_smartcard_service_initable_init_finish;
}

static char *
module_get_path (GckModule *module)
{
        return p11_kit_config_option (gck_module_get_functions (module), "module");
}

static char *
get_object_path_for_token (GsdSmartcardService *self,
                           GckSlot             *slot)
{
        g_autofree char *module_path = NULL;
        g_autofree char *escaped_library_path = NULL;
        g_autoptr(GckModule) module = NULL;

        module = gck_slot_get_module (slot);
        module_path = module_get_path (module);
        escaped_library_path = gsd_smartcard_utils_escape_object_path (module_path);

        return g_strdup_printf ("%s/token_from_%s_slot_%lu",
                                GSD_SMARTCARD_MANAGER_TOKENS_DBUS_PATH,
                                escaped_library_path,
                                gck_slot_get_handle (slot));
}

static gboolean
gsd_smartcard_service_handle_get_login_token (GsdSmartcardServiceManager *manager,
                                              GDBusMethodInvocation      *invocation)
{
        GsdSmartcardService *self = GSD_SMARTCARD_SERVICE (manager);
        g_autoptr(GckSlot) card_slot = NULL;
        g_autofree char *object_path = NULL;

        card_slot = gsd_smartcard_manager_get_login_token (self->smartcard_manager);

        if (card_slot == NULL) {
                const char *login_token_object_path;

                /* If we know there's a login token but it was removed before the
                 * smartcard manager could examine it, just return the generic login
                 * token object path
                 */
                login_token_object_path = get_login_token_object_path (self);

                if (g_hash_table_contains (self->tokens, login_token_object_path)) {
                        gsd_smartcard_service_manager_complete_get_login_token (manager,
                                                                                invocation,
                                                                                login_token_object_path);
                        return TRUE;
                }

                g_dbus_method_invocation_return_error (invocation,
                                                       GSD_SMARTCARD_MANAGER_ERROR,
                                                       GSD_SMARTCARD_MANAGER_ERROR_FINDING_SMARTCARD,
                                                       _("User was not logged in with smartcard."));

                return TRUE;
        }

        object_path = get_object_path_for_token (self, card_slot);
        gsd_smartcard_service_manager_complete_get_login_token (manager,
                                                                invocation,
                                                                object_path);
        return TRUE;
}

static gboolean
gsd_smartcard_service_handle_get_inserted_tokens (GsdSmartcardServiceManager *manager,
                                                  GDBusMethodInvocation      *invocation)
{
        GsdSmartcardService *self = GSD_SMARTCARD_SERVICE (manager);
        g_autolist(GckSlot) inserted_tokens = NULL;
        g_autoptr(GPtrArray) object_paths = NULL;
        GList *node;

        inserted_tokens = gsd_smartcard_manager_get_inserted_tokens (self->smartcard_manager,
                                                                     NULL);

        object_paths = g_ptr_array_new_with_free_func (g_free);
        for (node = inserted_tokens; node != NULL; node = node->next) {
                GckSlot *card_slot = node->data;
                char *object_path;

                object_path = get_object_path_for_token (self, card_slot);
                g_ptr_array_add (object_paths, object_path);
        }
        g_ptr_array_add (object_paths, NULL);

        gsd_smartcard_service_manager_complete_get_inserted_tokens (manager,
                                                                    invocation,
                                                                    (const char * const *) object_paths->pdata);

        return TRUE;
}

static void
smartcard_service_manager_interface_init (GsdSmartcardServiceManagerIface *interface)
{
        interface->handle_get_login_token = gsd_smartcard_service_handle_get_login_token;
        interface->handle_get_inserted_tokens = gsd_smartcard_service_handle_get_inserted_tokens;
}

static void
gsd_smartcard_service_init (GsdSmartcardService *self)
{
        self->tokens = g_hash_table_new_full (g_str_hash,
                                              g_str_equal,
                                              (GDestroyNotify) g_free,
                                              NULL);
}

static void
gsd_smartcard_service_dispose (GObject *object)
{
        GsdSmartcardService *self = GSD_SMARTCARD_SERVICE (object);

        g_clear_handle_id (&self->name_id, g_bus_unown_name);

        g_clear_object (&self->bus_connection);
        g_clear_object (&self->object_manager_server);
        g_clear_object (&self->smartcard_manager);

        g_cancellable_cancel (self->cancellable);
        g_clear_object (&self->cancellable);
        g_clear_pointer (&self->tokens, g_hash_table_unref);

        G_OBJECT_CLASS (gsd_smartcard_service_parent_class)->dispose (object);
}

static void
gsd_smartcard_service_set_property (GObject      *object,
                                    guint         property_id,
                                    const GValue *value,
                                    GParamSpec   *param_spec)
{
        GsdSmartcardService *self = GSD_SMARTCARD_SERVICE (object);

        switch (property_id) {
                case PROP_MANAGER:
                        self->smartcard_manager = g_value_dup_object (value);
                        break;
                default:
                        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, param_spec);
                        break;
        }
}

static void
gsd_smartcard_service_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *param_spec)
{
        GsdSmartcardService *self = GSD_SMARTCARD_SERVICE (object);

        switch (property_id) {
                case PROP_MANAGER:
                        g_value_set_object (value, self->smartcard_manager);
                        break;
                case PROP_BUS_CONNECTION:
                        g_value_set_object (value, self->bus_connection);
                        break;
                default:
                        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, param_spec);
                        break;
        }
}

static void
gsd_smartcard_service_class_init (GsdSmartcardServiceClass *service_class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (service_class);
        GParamSpec *param_spec;

        object_class->dispose = gsd_smartcard_service_dispose;
        object_class->set_property = gsd_smartcard_service_set_property;
        object_class->get_property = gsd_smartcard_service_get_property;

        param_spec = g_param_spec_object ("manager",
                                          "Smartcard Manager",
                                          "Smartcard Manager",
                                          GSD_TYPE_SMARTCARD_MANAGER,
                                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
        g_object_class_install_property (object_class, PROP_MANAGER, param_spec);
        param_spec = g_param_spec_object ("bus-connection",
                                          "Bus Connection",
                                          "bus connection",
                                          G_TYPE_DBUS_CONNECTION,
                                          G_PARAM_READABLE);
        g_object_class_install_property (object_class, PROP_BUS_CONNECTION, param_spec);
}

static void
on_new_async_finished (GObject      *source_object,
                       GAsyncResult *result,
                       gpointer      user_data)
{
        g_autoptr(GTask) task = g_steal_pointer (&user_data);
        GError *error = NULL;
        GObject *object;

        object = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object),
                                              result,
                                              &error);

        if (object == NULL) {
                g_task_return_error (task, error);
                return;
        }

        g_assert (GSD_IS_SMARTCARD_SERVICE (object));

        g_task_return_pointer (task, object, g_object_unref);
}

void
gsd_smartcard_service_new_async (GsdSmartcardManager *manager,
                                 GCancellable        *cancellable,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
        GTask *task;

        task = g_task_new (NULL, cancellable, callback, user_data);

        g_async_initable_new_async (GSD_TYPE_SMARTCARD_SERVICE,
                                    G_PRIORITY_DEFAULT,
                                    cancellable,
                                    (GAsyncReadyCallback) on_new_async_finished,
                                    task,
                                    "manager", manager,
                                    NULL);
}

GsdSmartcardService *
gsd_smartcard_service_new_finish (GAsyncResult  *result,
                                  GError       **error)
{
        GTask *task;
        GsdSmartcardService *self = NULL;

        task = G_TASK (result);

        self = g_task_propagate_pointer (task, error);

        if (self == NULL)
                return self;

        return g_object_ref (self);
}

static char *
get_object_path_for_module_path (GsdSmartcardService *self,
                                 const char          *module_path)
{
        char *object_path;
        g_autofree char *escaped_library_path = NULL;

        escaped_library_path = gsd_smartcard_utils_escape_object_path (module_path);

        object_path = g_build_path ("/",
                                    GSD_SMARTCARD_MANAGER_DRIVERS_DBUS_PATH,
                                    escaped_library_path, NULL);

        return object_path;
}

static char *
get_object_path_for_module (GsdSmartcardService *self,
                            GckModule           *module)
{
        g_autofree char *module_path = NULL;

        module_path = module_get_path (module);

        return get_object_path_for_module_path (self, module_path);
}

void
gsd_smartcard_service_register_driver (GsdSmartcardService *self,
                                       GckModule           *module)
{
        g_autoptr(GDBusObjectSkeleton) object = NULL;
        g_autoptr(GDBusInterfaceSkeleton) interface = NULL;
        g_autoptr(GckModuleInfo) module_info = NULL;
        g_autofree char *object_path = NULL;
        g_autofree char *module_path = NULL;
        const char *module_description;

        module_path = module_get_path (module);
        object_path = get_object_path_for_module_path (self, module_path);
        object = G_DBUS_OBJECT_SKELETON (gsd_smartcard_service_object_skeleton_new (object_path));

        interface = G_DBUS_INTERFACE_SKELETON (gsd_smartcard_service_driver_skeleton_new ());
        g_dbus_object_skeleton_add_interface (object, interface);

        module_info = gck_module_get_info (module);
        module_description = module_info ? module_info->library_description : NULL;

        g_debug ("Registering driver %s", module_description);

        g_object_set (G_OBJECT (interface),
                      "library", module_path,
                      "description", module_description,
                      NULL);
        g_dbus_object_manager_server_export (self->object_manager_server,
                                             object);
}

static void
synchronize_token_now (GsdSmartcardService *self,
                       GckSlot             *card_slot)
{
        GDBusInterfaceSkeleton *interface;
        g_autoptr(GckTokenInfo) token_info = NULL;
        g_autoptr(GMutexLocker) locked = NULL;
        g_autofree char *object_path = NULL;
        g_autofree char *token_name = NULL;
        gboolean was_present, is_present, is_login_card;

        object_path = get_object_path_for_token (self, card_slot);

        locked = g_mutex_locker_new (&G_LOCK_NAME (gsd_smartcard_tokens));
        interface = g_hash_table_lookup (self->tokens, object_path);

        if (interface == NULL)
                return;

        g_object_get (G_OBJECT (interface),
                      "is-inserted", &was_present,
                      NULL);

        is_present = gck_slot_has_flags (card_slot, CKF_TOKEN_PRESENT);

        if (was_present) {
                g_object_get (G_OBJECT (interface),
                              "name", &token_name,
                              NULL);
        } else if (is_present) {
                token_info = gck_slot_get_token_info (card_slot);

                if (token_info)
                        token_name = g_strdup (token_info->label);
        }

        if (g_strcmp0 (gsd_smartcard_utils_get_login_token_name (), token_name) == 0)
                is_login_card = TRUE;
        else
                is_login_card = FALSE;

        g_debug ("===============================");
        g_debug (" Token '%s'", token_name);
        g_debug (" Inserted: %s", is_present? "yes" : "no");
        g_debug (" Previously used to login: %s", is_login_card? "yes" : "no");
        g_debug ("===============================\n");

        if (!is_present && is_login_card) {
                if (was_present)
                        gsd_smartcard_manager_do_remove_action (self->smartcard_manager);
        }

        g_object_set (G_OBJECT (interface),
                      "used-to-login", is_login_card,
                      "is-inserted", is_present,
                      NULL);
        g_object_get (G_OBJECT (interface),
                      "used-to-login", &is_login_card,
                      "is-inserted", &is_present,
                      NULL);

        if (is_login_card && !self->login_token_bound) {
                const char *login_token_path;
                GDBusInterfaceSkeleton *login_token_interface;

                login_token_path = get_login_token_object_path (self);
                login_token_interface = g_hash_table_lookup (self->tokens, login_token_path);

                if (login_token_interface != NULL) {
                        g_object_bind_property (interface, "driver",
                                                login_token_interface, "driver",
                                                G_BINDING_SYNC_CREATE);
                        g_object_bind_property (interface, "is-inserted",
                                                login_token_interface, "is-inserted",
                                                G_BINDING_SYNC_CREATE);
                        self->login_token_bound = TRUE;
                }
        }
}

typedef struct
{
        GckSlot      *card_slot;
        GckTokenInfo *token_info;
        char         *object_path;
        GSource      *main_thread_source;
} RegisterNewTokenOperation;

static void
destroy_register_new_token_operation (RegisterNewTokenOperation *operation)
{
        g_clear_pointer (&operation->main_thread_source,
                         g_source_destroy);
        g_clear_object (&operation->card_slot);
        g_clear_pointer (&operation->token_info, gck_token_info_free);
        g_free (operation->object_path);
        g_free (operation);
}

static gboolean
on_main_thread_to_register_new_token (GTask *task)
{
        GsdSmartcardService *self;
        RegisterNewTokenOperation *operation;
        g_autoptr(GDBusObjectSkeleton) object = NULL;
        g_autoptr(GDBusInterfaceSkeleton) interface = NULL;
        g_autoptr(GckModule) module = NULL;
        g_autofree char *driver_object_path = NULL;
        const char *token_name;

        self = g_task_get_source_object (task);

        operation = g_task_get_task_data (task);
        operation->main_thread_source = NULL;

        object = G_DBUS_OBJECT_SKELETON (gsd_smartcard_service_object_skeleton_new (operation->object_path));
        interface = G_DBUS_INTERFACE_SKELETON (gsd_smartcard_service_token_skeleton_new ());

        g_dbus_object_skeleton_add_interface (object, interface);

        module = gck_slot_get_module (operation->card_slot);
        driver_object_path = get_object_path_for_module (self, module);
        token_name = operation->token_info->label;

        g_object_set (G_OBJECT (interface),
                      "driver", driver_object_path,
                      "name", token_name,
                      NULL);

        g_dbus_object_manager_server_export (self->object_manager_server,
                                             object);

        G_LOCK (gsd_smartcard_tokens);
        g_hash_table_insert (self->tokens, g_strdup (operation->object_path), interface);
        G_UNLOCK (gsd_smartcard_tokens);

        g_task_return_boolean (task, TRUE);
        g_object_unref (task);

        return G_SOURCE_REMOVE;
}

static void
create_main_thread_source (GSourceFunc   callback,
                           gpointer      user_data,
                           GSource     **source_out)
{
        g_autoptr(GSource) source = NULL;

        source = g_idle_source_new ();
        g_source_set_callback (source, callback, user_data, NULL);

        *source_out = source;
        g_source_attach (source, NULL);
}

static void
register_new_token_in_main_thread (GsdSmartcardService *self,
                                   GckSlot             *card_slot,
                                   char                *object_path,
                                   GCancellable        *cancellable,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
        RegisterNewTokenOperation *operation;
        GTask *task;

        operation = g_new0 (RegisterNewTokenOperation, 1);
        operation->card_slot = g_object_ref (card_slot);
        operation->token_info = gck_slot_get_token_info (card_slot);
        operation->object_path = g_strdup (object_path);

        task = g_task_new (self, cancellable, callback, user_data);

        g_task_set_task_data (task,
                              operation,
                              (GDestroyNotify) destroy_register_new_token_operation);

        create_main_thread_source ((GSourceFunc) on_main_thread_to_register_new_token,
                                   task,
                                   &operation->main_thread_source);
}

static gboolean
register_new_token_in_main_thread_finish (GsdSmartcardService  *self,
                                          GAsyncResult         *result,
                                          GError              **error)
{
        return g_task_propagate_boolean (G_TASK (result), error);
}

static void
on_token_registered (GsdSmartcardService *self,
                     GAsyncResult        *result,
                     gpointer             user_data)
{
        g_autoptr(GckSlot) card_slot = g_steal_pointer (&user_data);
        gboolean registered;
        GError *error = NULL;

        registered = register_new_token_in_main_thread_finish (self, result, &error);

        if (!registered) {
                g_debug ("Couldn't register token: %s",
                         error->message);
                return;
        }

        synchronize_token_now (self, card_slot);
}

typedef struct
{
        GckSlot *card_slot;
        GSource *main_thread_source;
} SynchronizeTokenOperation;

static void
destroy_synchronize_token_operation (SynchronizeTokenOperation *operation)
{
        g_clear_pointer (&operation->main_thread_source,
                         g_source_destroy);
        g_clear_object (&operation->card_slot);
        g_free (operation);
}

static gboolean
on_main_thread_to_synchronize_token (GTask *task)
{
        GsdSmartcardService *self;
        SynchronizeTokenOperation *operation;

        self = g_task_get_source_object (task);

        operation = g_task_get_task_data (task);
        operation->main_thread_source = NULL;

        synchronize_token_now (self, operation->card_slot);

        g_task_return_boolean (task, TRUE);
        g_object_unref (task);

        return G_SOURCE_REMOVE;
}

static gboolean
synchronize_token_in_main_thread_finish (GsdSmartcardService  *self,
                                         GAsyncResult         *result,
                                         GError              **error)
{
        return g_task_propagate_boolean (G_TASK (result), error);
}

static void
synchronize_token_in_main_thread (GsdSmartcardService *self,
                                  GckSlot             *card_slot,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
        SynchronizeTokenOperation *operation;
        GTask *task;

        operation = g_new0 (SynchronizeTokenOperation, 1);
        operation->card_slot = g_object_ref (card_slot);

        task = g_task_new (self, cancellable, callback, user_data);

        g_task_set_task_data (task,
                              operation,
                              (GDestroyNotify)
                              destroy_synchronize_token_operation);

        create_main_thread_source ((GSourceFunc)
                                   on_main_thread_to_synchronize_token,
                                   task,
                                   &operation->main_thread_source);
}

static void
on_token_synchronized (GsdSmartcardService *self,
                       GAsyncResult        *result,
                       gpointer             user_data)
{
        gboolean synchronized;
        GError *error = NULL;

        synchronized = synchronize_token_in_main_thread_finish (self, result, &error);

        if (!synchronized)
                g_debug ("Couldn't synchronize token: %s", error->message);
}

void
gsd_smartcard_service_sync_token (GsdSmartcardService *self,
                                  GckSlot             *card_slot,
                                  GCancellable        *cancellable)
{
        g_autofree char *object_path = NULL;
        GDBusInterfaceSkeleton *interface;

        object_path = get_object_path_for_token (self, card_slot);

        G_LOCK (gsd_smartcard_tokens);
        interface = g_hash_table_lookup (self->tokens, object_path);
        G_UNLOCK (gsd_smartcard_tokens);

        if (interface == NULL)
                register_new_token_in_main_thread (self,
                                                   card_slot,
                                                   object_path,
                                                   cancellable,
                                                   (GAsyncReadyCallback)
                                                   on_token_registered,
                                                   g_object_ref (card_slot));

        else
                synchronize_token_in_main_thread (self,
                                                  card_slot,
                                                  cancellable,
                                                  (GAsyncReadyCallback)
                                                  on_token_synchronized,
                                                  NULL);
}
