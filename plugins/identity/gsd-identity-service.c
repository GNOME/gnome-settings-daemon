/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors: Ray Strode
 */

#include "config.h"

#include "gsd-identity-service.h"
#include "org.gnome.Identity.h"
#include "gsd-identity-manager.h"
#include "gsd-kerberos-identity.h"
#include "gsd-kerberos-identity-manager.h"
#include "gsd-identity-enum-types.h"

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

struct _GsdIdentityServicePrivate
{
        GDBusConnection           *bus_connection;
        GDBusObjectManagerServer  *object_manager_server;
        GsdIdentityServiceManager *server_manager;
        GsdIdentityManager        *identity_manager;
        GCancellable              *cancellable;
};

enum {
        PROP_0,
        PROP_BUS_CONNECTION
};

enum {
        HANDLE_SIGN_IN,
        HANDLE_SIGN_OUT,
        NUMBER_OF_SIGNALS

};

static guint signals[NUMBER_OF_SIGNALS] = { 0 };

static void gsd_identity_service_set_property (GObject       *object,
                                               guint          property_id,
                                               const GValue  *value,
                                               GParamSpec    *param_spec);
static void gsd_identity_service_get_property (GObject    *object,
                                               guint       property_id,
                                               GValue     *value,
                                               GParamSpec *param_spec);
static void initable_interface_init (GInitableIface *interface);
G_DEFINE_TYPE_WITH_CODE (GsdIdentityService,
                         gsd_identity_service,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_interface_init));
static char *
escape_object_path_component (const char *data,
                              gsize       length)
{
       const char *p;
       char *object_path;
       GString *string;

       g_return_val_if_fail (data != NULL, NULL);

       string = g_string_sized_new ((length + 1) * 6);

       for (p = data; *p != '\0'; p++)
       {
               guchar character;

               character = (guchar) *p;

               if (((character >= ((guchar) 'a')) &&
                    (character <= ((guchar) 'z'))) ||
                   ((character >= ((guchar) 'A')) &&
                    (character <= ((guchar) 'Z'))) ||
                   ((character >= ((guchar) '0')) &&
                    (character <= ((guchar) '9'))))
               {
                       g_string_append_c (string, (char) character);
                       continue;
               }

               g_string_append_printf (string, "_%x_", character);
       }

       object_path = string->str;

       g_string_free (string, FALSE);

       return object_path;
}

static char *
get_object_path_for_identity (GsdIdentityService *self,
                              GsdIdentity        *identity)
{
        char    **components;
        GString  *object_path;
        int       i;

        components = gsd_identity_get_identifier_components (identity);
        object_path = g_string_new ("/org/gnome/Identity/Manager");

        for (i = 0; components[i] != NULL; i++) {
                char *escaped_component;

                g_string_append_c (object_path, '/');
                escaped_component = escape_object_path_component (components[i],
                                                                  (gsize) strlen (components[i]));

                g_string_append (object_path, escaped_component);

                g_free (escaped_component);
        }
        g_strfreev (components);

        return g_string_free (object_path, FALSE);
}

static char *
export_identity (GsdIdentityService *self,
                 GsdIdentity        *identity)
{
        char *object_path;
        GDBusObjectSkeleton *object;
        GDBusInterfaceSkeleton *interface;

        object_path = get_object_path_for_identity (self, identity);
        object = G_DBUS_OBJECT_SKELETON (gsd_identity_service_object_skeleton_new (object_path));
        interface = G_DBUS_INTERFACE_SKELETON (gsd_identity_service_org_gnome_identity_skeleton_new ());

        g_object_bind_property (G_OBJECT (identity),
                                "identifier",
                                G_OBJECT (interface),
                                "identifier",
                                G_BINDING_DEFAULT);

        g_object_bind_property (G_OBJECT (identity),
                                "expiration-timestamp",
                                G_OBJECT (interface),
                                "expiration-timestamp",
                                G_BINDING_DEFAULT);

        g_dbus_object_skeleton_add_interface (object, interface);
        g_object_unref (interface);

        g_dbus_object_manager_server_export (self->priv->object_manager_server,
                                             object);
        g_object_unref (object);

        return object_path;
}

static void
unexport_identity (GsdIdentityService *self,
                   GsdIdentity        *identity)
{
        char *object_path;

        object_path = get_object_path_for_identity (self, identity);
        g_dbus_object_manager_server_unexport (self->priv->object_manager_server,
                                               object_path);
        g_free (object_path);
}

static void
on_identity_added (GsdIdentityManager  *manager,
                   GsdIdentity         *identity,
                   GsdIdentityService  *self)
{
        export_identity (self, identity);
}

static void
on_identity_removed (GsdIdentityManager  *manager,
                     GsdIdentity         *identity,
                     GsdIdentityService  *self)
{
        unexport_identity (self, identity);
}

static void
on_identity_expired (GsdIdentityManager  *manager,
                     GsdIdentity         *identity,
                     GsdIdentityService  *self)
{
}

static void
on_identity_refreshed (GsdIdentityManager  *manager,
                       GsdIdentity         *identity,
                       GsdIdentityService  *self)
{
}

static void
on_identity_renamed (GsdIdentityManager  *manager,
                     GsdIdentity         *identity,
                     GsdIdentityService  *self)
{
}

static void
on_identities_listed (GsdIdentityManager   *manager,
                      GAsyncResult         *result,
                      GsdIdentityService   *self)
{
        GError *error = NULL;
        GList *identities, *node;

        g_signal_connect (manager,
                          "identity-added",
                          G_CALLBACK (on_identity_added),
                          self);

        g_signal_connect (manager,
                          "identity-removed",
                          G_CALLBACK (on_identity_removed),
                          self);

        g_signal_connect (manager,
                          "identity-expired",
                          G_CALLBACK (on_identity_expired),
                          self);

        g_signal_connect (manager,
                          "identity-refreshed",
                          G_CALLBACK (on_identity_refreshed),
                          self);

        g_signal_connect (manager,
                          "identity-renamed",
                          G_CALLBACK (on_identity_renamed),
                          self);

        identities = gsd_identity_manager_list_identities_finish (manager,
                                                                 result,
                                                                 &error);

        if (identities == NULL) {
                if (error != NULL) {
                        g_warning ("UmUserPanel: Could not list identities: %s",
                                   error->message);
                        g_error_free (error);
                }
                return;
        }

        for (node = identities; node != NULL; node = node->next) {
                GsdIdentity *identity = node->data;

                export_identity (self, identity);
        }
}

static void
on_sign_in_handled (GsdIdentityService     *self,
                    GSimpleAsyncResult     *result,
                    GDBusMethodInvocation  *invocation)
{
        GError *error;
        GsdIdentity *identity;
        char *object_path;

        error = NULL;
        if (g_simple_async_result_propagate_error (result,
                                                   &error)) {
                g_dbus_method_invocation_take_error (invocation, error);
                return;
        }

        identity = GSD_IDENTITY (g_simple_async_result_get_op_res_gpointer (result));
        object_path = export_identity (self, identity);
        g_object_unref (identity);

        g_dbus_method_invocation_return_value (invocation,
                                               g_variant_new ("(o)", object_path));
        g_free (object_path);
}

static gboolean
handle_sign_in (GsdIdentityServiceManager *manager,
                GDBusMethodInvocation     *invocation,
                const char                *identifier,
                GVariant                  *details,
                GsdIdentityService        *self)
{
        GSimpleAsyncResult *result;
        GCancellable *cancellable;

        result = g_simple_async_result_new (G_OBJECT (self),
                                            (GAsyncReadyCallback)
                                            on_sign_in_handled,
                                            invocation,
                                            handle_sign_in);

        /* FIXME: should we cancel this if the invoker falls off the bus ?*/
        cancellable = g_cancellable_new ();
        g_simple_async_result_set_check_cancellable (result, cancellable);

        g_signal_emit (G_OBJECT (self),
                       signals[HANDLE_SIGN_IN],
                       0,
                       identifier,
                       details,
                       cancellable,
                       result);

        return TRUE;
}

static void
on_sign_out_handled (GsdIdentityService     *self,
                     GSimpleAsyncResult     *result,
                     GDBusMethodInvocation  *invocation)
{
        GError *error;

        error = NULL;
        if (g_simple_async_result_propagate_error (result,
                                                   &error)) {
                g_dbus_method_invocation_take_error (invocation, error);
                return;
        }


        g_dbus_method_invocation_return_value (invocation, NULL);
}

static gboolean
handle_sign_out (GsdIdentityServiceManager *manager,
                 GDBusMethodInvocation     *invocation,
                 const char                *identifier,
                 GsdIdentityService        *self)
{
        GSimpleAsyncResult *result;
        GCancellable *cancellable;

        result = g_simple_async_result_new (G_OBJECT (self),
                                            (GAsyncReadyCallback)
                                            on_sign_out_handled,
                                            invocation,
                                            handle_sign_out);

        cancellable = g_cancellable_new ();
        g_simple_async_result_set_check_cancellable (result, cancellable);

        g_signal_emit (G_OBJECT (self),
                       signals[HANDLE_SIGN_OUT],
                       0,
                       identifier,
                       cancellable,
                       result);

        return TRUE;
}

static gboolean
gsd_identity_service_initable_init (GInitable     *initable,
                                   GCancellable   *cancellable,
                                   GError        **error)
{
        GsdIdentityService *self = GSD_IDENTITY_SERVICE (initable);
        GsdIdentityServiceObjectSkeleton *object;

        if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
                return FALSE;
        }

        g_return_val_if_fail (self->priv->bus_connection != NULL, FALSE);

        self->priv->object_manager_server = g_dbus_object_manager_server_new ("/org/gnome/Identity");

        self->priv->server_manager = gsd_identity_service_manager_skeleton_new ();
        g_signal_connect (self->priv->server_manager,
                          "handle-sign-in",
                          G_CALLBACK (handle_sign_in),
                          self);
        g_signal_connect (self->priv->server_manager,
                          "handle-sign-out",
                          G_CALLBACK (handle_sign_out),
                          self);

        object = gsd_identity_service_object_skeleton_new ("/org/gnome/Identity/Manager");
        gsd_identity_service_object_skeleton_set_manager (object,
                                                         self->priv->server_manager);

        g_dbus_object_manager_server_export (self->priv->object_manager_server,
                                             G_DBUS_OBJECT_SKELETON (object));
        g_object_unref (object);

        g_dbus_object_manager_server_set_connection (self->priv->object_manager_server,
                                                     self->priv->bus_connection);

        self->priv->identity_manager = gsd_kerberos_identity_manager_new (cancellable, error);
        if (self->priv->identity_manager == NULL) {
                return FALSE;
        }

        self->priv->cancellable = g_cancellable_new ();
        gsd_identity_manager_list_identities (self->priv->identity_manager,
                                              self->priv->cancellable,
                                              (GAsyncReadyCallback)
                                              on_identities_listed, self);

        return TRUE;
}

static void
initable_interface_init (GInitableIface *interface)
{
        interface->init = gsd_identity_service_initable_init;
}

static void
gsd_identity_service_init (GsdIdentityService *self)
{
        self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                                  GSD_TYPE_IDENTITY_SERVICE,
                                                  GsdIdentityServicePrivate);
}

static void
gsd_identity_service_dispose (GObject *object)
{
        GsdIdentityService *self = GSD_IDENTITY_SERVICE (object);

        g_clear_object (&self->priv->bus_connection);
        g_clear_object (&self->priv->object_manager_server);
        g_clear_object (&self->priv->server_manager);
        g_clear_object (&self->priv->identity_manager);

        g_cancellable_cancel (self->priv->cancellable);
        g_clear_object (&self->priv->cancellable);

        G_OBJECT_CLASS (gsd_identity_service_parent_class)->dispose (object);
}

static void
gsd_identity_service_finalize (GObject *object)
{
        G_OBJECT_CLASS (gsd_identity_service_parent_class)->finalize (object);
}

static void
gsd_identity_service_set_property (GObject       *object,
                                  guint           property_id,
                                  const GValue   *value,
                                  GParamSpec     *param_spec)
{
        GsdIdentityService *self = GSD_IDENTITY_SERVICE (object);

        switch (property_id) {
                case PROP_BUS_CONNECTION:
                        self->priv->bus_connection = g_value_dup_object (value);
                        break;

                default:
                        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, param_spec);
                        break;
        }
}

static void
gsd_identity_service_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *param_spec)
{
        GsdIdentityService *self = GSD_IDENTITY_SERVICE (object);

        switch (property_id) {
                case PROP_BUS_CONNECTION:
                        g_value_set_object (value, self->priv->bus_connection);
                        break;

                default:
                        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, param_spec);
                        break;
        }
}

static char *
dashed_string_to_studly_caps (const char *dashed_string)
{
        char *studly_string;;
        size_t studly_string_length;
        size_t i;

        i = 0;

        studly_string = g_strdup (dashed_string);
        studly_string_length = strlen (studly_string);

        studly_string[i] = g_ascii_toupper (studly_string[i]);
        i++;

        while (i < studly_string_length) {
                if (studly_string[i] == '-' || studly_string[i] == '_') {
                        memmove (studly_string + i,
                                 studly_string + i + 1,
                                 studly_string_length - i - 1);
                        studly_string_length--;
                        if (g_ascii_isalpha (studly_string[i])) {
                                studly_string[i] = g_ascii_toupper (studly_string[i]);
                        }
                }
                i++;
        }
        studly_string[studly_string_length] = '\0';

        return studly_string;
}

static char *
dashed_string_to_dbus_error_string (const char *dashed_string,
                                    const char *old_prefix,
                                    const char *new_prefix,
                                    const char *suffix)
{
        char *studly_suffix;
        char *dbus_error_string;
        size_t dbus_error_string_length;
        size_t i;

        i = 0;

        if (g_str_has_prefix (dashed_string, old_prefix) &&
            (dashed_string[strlen(old_prefix)] == '-' ||
             dashed_string[strlen(old_prefix)] == '_')) {
                dashed_string += strlen (old_prefix) + 1;
        }

        studly_suffix = dashed_string_to_studly_caps (suffix);
        dbus_error_string = g_strdup_printf ("%s.%s.%s", new_prefix, dashed_string, studly_suffix);
        g_free (studly_suffix);
        i += strlen (new_prefix) + 1;

        dbus_error_string_length = strlen (dbus_error_string);

        dbus_error_string[i] = g_ascii_toupper (dbus_error_string[i]);
        i++;

        while (i < dbus_error_string_length) {
                if (dbus_error_string[i] == '_' || dbus_error_string[i] == '-') {
                        dbus_error_string[i] = '.';

                        if (g_ascii_isalpha (dbus_error_string[i + 1])) {
                                dbus_error_string[i + 1] =  g_ascii_toupper (dbus_error_string[i + 1]);
                        }
                }

                i++;
        }

        return dbus_error_string;
}

static void
register_error_domain (GQuark error_domain,
                       GType  error_enum)
{
        const char *error_domain_string;
        char *type_name;
        GType type;
        GTypeClass *type_class;
        GEnumClass *enum_class;
        guint i;

        error_domain_string = g_quark_to_string (error_domain);
        type_name = dashed_string_to_studly_caps (error_domain_string);
        type = g_type_from_name (type_name);
        type_class = g_type_class_ref (type);
        enum_class = G_ENUM_CLASS (type_class);

        for (i = 0; i < enum_class->n_values; i++) {
                char *dbus_error_string;

                dbus_error_string = dashed_string_to_dbus_error_string (error_domain_string,
                                                                        "gsd",
                                                                        "org.gnome",
                                                                        enum_class->values[i].value_nick);

                g_debug ("GsdIdentityService: Registering dbus error %s", dbus_error_string);
                g_dbus_error_register_error (error_domain,
                                             enum_class->values[i].value,
                                             dbus_error_string);
                g_free (dbus_error_string);
        }

        g_type_class_unref (type_class);
}

static void
gsd_identity_service_class_init (GsdIdentityServiceClass *service_class)
{
        GObjectClass *object_class = G_OBJECT_CLASS (service_class);
        GParamSpec   *param_spec;

        object_class->dispose = gsd_identity_service_dispose;
        object_class->finalize = gsd_identity_service_finalize;
        object_class->set_property = gsd_identity_service_set_property;
        object_class->get_property = gsd_identity_service_get_property;

        signals[HANDLE_SIGN_IN] = g_signal_new ("handle-sign-in",
                                                G_TYPE_FROM_CLASS (service_class),
                                                G_SIGNAL_RUN_LAST,
                                                G_STRUCT_OFFSET (GsdIdentityServiceClass, handle_sign_in),
                                                NULL, NULL, NULL,
                                                G_TYPE_NONE, 4,
                                                G_TYPE_STRING,
                                                G_TYPE_VARIANT,
                                                G_TYPE_CANCELLABLE,
                                                G_TYPE_SIMPLE_ASYNC_RESULT);
        signals[HANDLE_SIGN_OUT] = g_signal_new ("handle-sign-out",
                                                 G_TYPE_FROM_CLASS (service_class),
                                                 G_SIGNAL_RUN_LAST,
                                                 G_STRUCT_OFFSET (GsdIdentityServiceClass, handle_sign_out),
                                                 NULL, NULL, NULL,
                                                 G_TYPE_NONE, 3,
                                                 G_TYPE_STRING,
                                                 G_TYPE_CANCELLABLE,
                                                 G_TYPE_SIMPLE_ASYNC_RESULT);
        param_spec = g_param_spec_object ("bus-connection",
                                          "Bus Connection",
                                          "bus connection",
                                          G_TYPE_DBUS_CONNECTION,
                                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
        g_object_class_install_property (object_class, PROP_BUS_CONNECTION, param_spec);
        register_error_domain (GSD_IDENTITY_MANAGER_ERROR,
                               GSD_TYPE_IDENTITY_MANAGER_ERROR);
        register_error_domain (GSD_IDENTITY_ERROR,
                               GSD_TYPE_IDENTITY_ERROR);

        g_type_class_add_private (service_class, sizeof (GsdIdentityServicePrivate));
}

GsdIdentityService *
gsd_identity_service_new (GDBusConnection  *connection,
                          GCancellable     *cancellable,
                          GError          **error)
{
        GObject *object;

        object = g_object_new (GSD_TYPE_IDENTITY_SERVICE,
                               "bus-connection", connection,
                               NULL);

        if (!g_initable_init (G_INITABLE (object), cancellable, error)) {
                return NULL;
        }

        return GSD_IDENTITY_SERVICE (object);
}

