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

#ifdef HAVE_PACKAGEKIT
#include <packagekit-glib2/packagekit.h>
#endif

#include "gsd-updates-manager.h"
#include "gnome-settings-profile.h"

#define GSD_UPDATES_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_UPDATES_MANAGER, GsdUpdatesManagerPrivate))

struct GsdUpdatesManagerPrivate
{
        GSettings *settings_http;
        GSettings *settings_ftp;
        GSettings *settings_gsd;
        PkControl *control;
        guint      timeout;
};

static void gsd_updates_manager_class_init (GsdUpdatesManagerClass *klass);
static void gsd_updates_manager_init (GsdUpdatesManager *updates_manager);
static void gsd_updates_manager_finalize (GObject *object);

G_DEFINE_TYPE (GsdUpdatesManager, gsd_updates_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

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
        manager->priv->control = pk_control_new ();

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
