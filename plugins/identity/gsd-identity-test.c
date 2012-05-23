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

#include "gsd-identity-test.h"
#include "gsd-kerberos-identity-manager.h"
#include "gsd-kerberos-identity.h"
#include "gsd-identity-inquiry.h"

#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <krb5.h>

struct _GsdIdentityTestPrivate
{
        GsdIdentityManager *identity_manager;
        GCancellable       *cancellable;
};

G_DEFINE_TYPE (GsdIdentityTest, gsd_identity_test, G_TYPE_OBJECT);

static void
gsd_identity_test_init (GsdIdentityTest *self)
{
        GError *error;

        self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                                  GSD_TYPE_KERBEROS_IDENTITY_MANAGER,
                                                  GsdIdentityTestPrivate);

        error = NULL;
        self->priv->identity_manager = gsd_kerberos_identity_manager_new (NULL, &error);

        if (self->priv->identity_manager == NULL) {
                g_warning ("identity manager could not be created: %s", error->message);
        }

}

static void
gsd_identity_test_dispose (GObject *object)
{
        GsdIdentityTest *self = GSD_IDENTITY_TEST (object);

        g_clear_object (&self->priv->identity_manager);

        G_OBJECT_CLASS (gsd_identity_test_parent_class)->dispose (object);
}

static void
gsd_identity_test_finalize (GObject *object)
{
        G_OBJECT_CLASS (gsd_identity_test_parent_class)->finalize (object);
}

static void
gsd_identity_test_class_init (GsdIdentityTestClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->dispose = gsd_identity_test_dispose;
        object_class->finalize = gsd_identity_test_finalize;

        g_type_class_add_private (klass, sizeof (GsdIdentityTestPrivate));
}

GsdIdentityTest *
gsd_identity_test_new (void)
{
        GObject *object = g_object_new (GSD_TYPE_IDENTITY_TEST, NULL);

        return GSD_IDENTITY_TEST (object);
}

static void
on_identity_renewed (GsdIdentityManager *identity_manager,
                     GAsyncResult       *result,
                     GsdIdentityTest    *test)
{
        GError *error;

        error = NULL;
        gsd_identity_manager_renew_identity_finish (identity_manager,
                                                    result,
                                                    &error);

        if (error != NULL) {
                g_warning ("Could not renew identity: %s",
                         error->message);
                g_error_free (error);
                return;
        }

        g_message ("identity renewed");
}

static void
on_identity_needs_renewal (GsdIdentityManager *identity_manager,
                           GsdIdentity        *identity,
                           GsdIdentityTest    *test)
{
        g_message ("identity needs renewal");
        gsd_identity_manager_renew_identity (identity_manager,
                                             identity,
                                             test->priv->cancellable,
                                             (GAsyncReadyCallback)
                                             on_identity_renewed,
                                             test);
}


typedef struct
{
        GsdIdentityTest *test;
        const char      *identifier;
} SignInRequest;

static void
on_identity_inquiry (GsdIdentityInquiry *inquiry,
                     GCancellable       *cancellable,
                     GsdIdentityTest    *test)
{
        GsdIdentityInquiryIter iter;
        GsdIdentityQuery *query;
        char *name, *banner;
        int fd;

        if (g_cancellable_is_cancelled (test->priv->cancellable)) {
                return;
        }

        name = gsd_identity_inquiry_get_name (inquiry);
        g_message ("name: %s", name);
        g_free (name);

        banner = gsd_identity_inquiry_get_banner (inquiry);
        g_message ("banner: %s", banner);
        g_free (banner);

        fd =  g_open ("/dev/tty", O_RDWR);
        gsd_identity_inquiry_iter_init (&iter, inquiry);
        while ((query = gsd_identity_inquiry_iter_next (&iter, inquiry)) != NULL) {
                char *prompt;
                char  answer[256] = "";
                ssize_t bytes_read;
                GsdIdentityQueryMode mode;

                if (g_cancellable_is_cancelled (test->priv->cancellable)) {
                        break;
                }

                prompt = gsd_identity_query_get_prompt (inquiry, query);
                g_message ("prompt: %s", prompt);
                g_free (prompt);

                mode = gsd_identity_query_get_mode (inquiry, query);
                if (mode == GSD_IDENTITY_QUERY_MODE_INVISIBLE) {
                        /* don't print user input to screen */
                        g_print ("\033[12h");
                }

                bytes_read = read (fd, answer, sizeof (answer) - 1);

                if (mode == GSD_IDENTITY_QUERY_MODE_INVISIBLE) {
                        /* restore visible input if we turned it off */
                        g_print ("\033[12l");
                }

                if (g_cancellable_is_cancelled (test->priv->cancellable)) {
                        break;
                }

                if (bytes_read > 0) {
                        /* Trim off \n */
                        answer[bytes_read - 1] = '\0';
                        g_message ("using password '%s'", answer);
                        gsd_identity_inquiry_answer_query (inquiry, query, answer);
                }
        }
        close (fd);
}

static void
on_identity_signed_in (GsdIdentityManager *identity_manager,
                       GAsyncResult       *result,
                       SignInRequest      *request)
{
        GError *error;

        error = NULL;
        gsd_identity_manager_sign_identity_in_finish (identity_manager,
                                                      result,
                                                      &error);

        if (error != NULL) {
                g_warning ("Could not sign-in identity %s: %s",
                           request->identifier, error->message);
                g_error_free (error);
                g_object_unref (request->test);
                g_slice_free (SignInRequest, request);
                return;
        }
        g_message ("identity %s signed in", request->identifier);

        g_object_unref (request->test);
        g_slice_free (SignInRequest, request);
}

static void
sign_in (GsdIdentityTest *test,
         const char      *identifier)
{
        SignInRequest *request;

        request = g_slice_new (SignInRequest);
        request->test = g_object_ref (test);
        request->identifier = identifier;

        gsd_identity_manager_sign_identity_in (test->priv->identity_manager,
                                               identifier,
                                               (GsdIdentityInquiryFunc)
                                               on_identity_inquiry,
                                               test,
                                               test->priv->cancellable,
                                               (GAsyncReadyCallback)
                                               on_identity_signed_in,
                                               request);
}

static void
on_identity_expiring (GsdIdentityManager *identity_manager,
                      GsdIdentity        *identity,
                      GsdIdentityTest    *test)
{
        const char *identifier;

        g_message ("identity about to expire");

        identifier = gsd_identity_get_identifier (identity);
        sign_in (test, identifier);
}

static void
on_identity_expired (GsdIdentityManager *identity_manager,
                     GsdIdentity        *identity,
                     GsdIdentityTest    *test)
{
        const char *identifier;

        g_message ("identity expired");

        identifier = gsd_identity_get_identifier (identity);
        sign_in (test, identifier);
}

void
gsd_identity_test_start (GsdIdentityTest  *test,
                         GError          **error)
{
        const char *principal_name;

        if (!g_cancellable_is_cancelled (test->priv->cancellable)) {
                g_warning ("identity test started more than once");
                g_cancellable_cancel (test->priv->cancellable);
                g_clear_object (&test->priv->cancellable);
        }

        test->priv->cancellable = g_cancellable_new ();

        g_signal_connect (G_OBJECT (test),
                          "identity-needs-renewal",
                          G_CALLBACK (on_identity_needs_renewal),
                          NULL);
        g_signal_connect (G_OBJECT (test),
                          "identity-expiring",
                          G_CALLBACK (on_identity_expiring),
                          NULL);
        g_signal_connect (G_OBJECT (test),
                          "identity-expired",
                          G_CALLBACK (on_identity_expired),
                          NULL);

        principal_name = g_getenv ("GSD_IDENTITY_TEST_PRINCIPAL");
        if (principal_name != NULL) {
                sign_in (test, principal_name);
        }
}

void
gsd_identity_test_stop (GsdIdentityTest *test)
{

        g_signal_handlers_disconnect_by_func (G_OBJECT (test),
                                              G_CALLBACK (on_identity_needs_renewal),
                                              NULL);
        g_signal_handlers_disconnect_by_func (G_OBJECT (test),
                                              G_CALLBACK (on_identity_expiring),
                                              NULL);
        g_signal_handlers_disconnect_by_func (G_OBJECT (test),
                                              G_CALLBACK (on_identity_expired),
                                              NULL);

        g_cancellable_cancel (test->priv->cancellable);
}
