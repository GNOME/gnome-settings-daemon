/**
 * Create a gnome-settings-daemon helper easily
 *
 * #define NEW gsd_media_keys_manager_new
 * #define START gsd_media_keys_manager_start
 * #define MANAGER GsdMediaKeysManager
 * #include "gsd-media-keys-manager.h"
 *
 * #include "daemon-skeleton.h"
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <locale.h>

#include <glib-unix.h>
#include <glib/gi18n.h>

#include "gnome-settings-bus.h"

#ifndef PLUGIN_NAME
#error Include PLUGIN_CFLAGS in the daemon s CFLAGS
#endif /* !PLUGIN_NAME */

#ifndef PLUGIN_DBUS_NAME
#error Include PLUGIN_DBUS_NAME in the daemon s CFLAGS
#endif /* !PLUGIN_DBUS_NAME */

#define GNOME_SESSION_DBUS_NAME                     "org.gnome.SessionManager"
#define GNOME_SESSION_CLIENT_PRIVATE_DBUS_INTERFACE "org.gnome.SessionManager.ClientPrivate"

static MANAGER *manager = NULL;
static int timeout = -1;
static char *dummy_name = NULL;
static gboolean verbose = FALSE;

static GOptionEntry entries[] = {
        { "exit-time", 0, 0, G_OPTION_ARG_INT, &timeout, "Exit after n seconds time", NULL },
        { "dummy-name", 0, 0, G_OPTION_ARG_STRING, &dummy_name, "Name when using the dummy daemon", NULL },
        { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, "Verbose", NULL },
        {NULL}
};

static void
respond_to_end_session (GDBusProxy *proxy)
{
        /* we must answer with "EndSessionResponse" */
        g_dbus_proxy_call (proxy, "EndSessionResponse",
                           g_variant_new ("(bs)", TRUE, ""),
                           G_DBUS_CALL_FLAGS_NONE,
                           -1, NULL, NULL, NULL);
}

static void
do_stop (GMainLoop *loop)
{
        g_main_loop_quit (loop);
}

static void
client_proxy_signal_cb (GDBusProxy *proxy,
                        gchar *sender_name,
                        gchar *signal_name,
                        GVariant *parameters,
                        gpointer user_data)
{
        if (g_strcmp0 (signal_name, "QueryEndSession") == 0) {
                g_debug ("Got QueryEndSession signal");
                respond_to_end_session (proxy);
        } else if (g_strcmp0 (signal_name, "EndSession") == 0) {
                g_debug ("Got EndSession signal");
                respond_to_end_session (proxy);
        } else if (g_strcmp0 (signal_name, "Stop") == 0) {
                g_debug ("Got Stop signal");
                do_stop (user_data);
        }
}

static void
on_client_registered (GObject             *source_object,
                      GAsyncResult        *res,
                      gpointer             user_data)
{
        GVariant *variant;
        GMainLoop *loop = user_data;
        GDBusProxy *client_proxy;
        GError *error = NULL;
        gchar *object_path = NULL;

        variant = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), res, &error);
        if (!variant) {
                g_warning ("Unable to register client: %s", error->message);
                g_error_free (error);
                return;
        }

        g_variant_get (variant, "(o)", &object_path);

        g_debug ("Registered client at path %s", object_path);

        client_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION, 0, NULL,
                                                      GNOME_SESSION_DBUS_NAME,
                                                      object_path,
                                                      GNOME_SESSION_CLIENT_PRIVATE_DBUS_INTERFACE,
                                                      NULL,
                                                      &error);
        if (!client_proxy) {
                g_warning ("Unable to get the session client proxy: %s", error->message);
                g_error_free (error);
                return;
        }

        g_signal_connect (client_proxy, "g-signal",
                          G_CALLBACK (client_proxy_signal_cb), loop);

        g_free (object_path);
        g_variant_unref (variant);
}

static void
register_with_gnome_session (GMainLoop *loop)
{
	GDBusProxy *proxy;
	const char *startup_id;

	proxy = G_DBUS_PROXY (gnome_settings_bus_get_session_proxy ());
	startup_id = g_getenv ("DESKTOP_AUTOSTART_ID");
	g_dbus_proxy_call (proxy,
			   "RegisterClient",
			   g_variant_new ("(ss)", dummy_name ? dummy_name : PLUGIN_NAME, startup_id ? startup_id : ""),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   NULL,
			   (GAsyncReadyCallback) on_client_registered,
			   loop);

	/* DESKTOP_AUTOSTART_ID must not leak into child processes, because
	 * it can't be reused. Child processes will not know whether this is
	 * a genuine value or erroneous already-used value. */
	g_unsetenv ("DESKTOP_AUTOSTART_ID");
}

static gboolean
handle_sigterm (gpointer user_data)
{
  GMainLoop *main_loop = user_data;

  g_debug ("Got SIGTERM; shutting down ...");

  if (g_main_loop_is_running (main_loop))
    g_main_loop_quit (main_loop);

  return G_SOURCE_REMOVE;
}

static void
install_signal_handler (GMainLoop *loop)
{
  g_autoptr(GSource) source = NULL;

  source = g_unix_signal_source_new (SIGTERM);

  g_source_set_callback (source, handle_sigterm, loop, NULL);
  g_source_attach (source, NULL);
}

static void
bus_acquired_cb (GDBusConnection *connection,
                 const gchar *name,
                 gpointer user_data G_GNUC_UNUSED)
{
        g_debug ("%s: acquired bus %p for name %s", G_STRFUNC, connection, name);
}

static void
name_acquired_cb (GDBusConnection *connection,
                  const gchar *name,
                  gpointer user_data G_GNUC_UNUSED)
{
        g_debug ("%s: acquired name %s on bus %p", G_STRFUNC, name, connection);
}

static void
name_lost_cb (GDBusConnection *connection,
              const gchar *name,
              gpointer user_data G_GNUC_UNUSED)
{
        g_debug ("%s: lost name %s on bus %p", G_STRFUNC, name, connection);
}

int
main (int argc, char **argv)
{
        GError *error = NULL;
        GOptionContext *context;
        GMainLoop *loop;
        guint name_own_id;

        bindtextdomain (GETTEXT_PACKAGE, GNOME_SETTINGS_LOCALEDIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);
        setlocale (LC_ALL, "");

        context = g_option_context_new (NULL);
        g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
        if (!g_option_context_parse (context, &argc, &argv, &error)) {
                fprintf (stderr, "%s\n", error->message);
                g_error_free (error);
                exit (1);
        }
        g_option_context_free (context);

	loop = g_main_loop_new (NULL, FALSE);

        if (verbose) {
                g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);
                /* Work around GLib not flushing the output for us by explicitly
                 * setting buffering to a sane behaviour. This is important
                 * during testing when the output is not going to a TTY and
                 * we are reading messages from g_debug on stdout.
                 *
                 * See also
                 *  https://bugzilla.gnome.org/show_bug.cgi?id=792432
                 */
                setlinebuf (stdout);
        }

	if (timeout > 0) {
		guint id;
		id = g_timeout_add_seconds (timeout, (GSourceFunc) g_main_loop_quit, loop);
		g_source_set_name_by_id (id, "[gnome-settings-daemon] g_main_loop_quit");
	}

        install_signal_handler (loop);

        manager = NEW ();
	register_with_gnome_session (loop);

        if (!START (manager, &error)) {
                fprintf (stderr, "Failed to start: %s\n", error->message);
                g_error_free (error);
                exit (1);
        }

	name_own_id = g_bus_own_name (G_BUS_TYPE_SESSION,
				      PLUGIN_DBUS_NAME,
				      G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE,
				      bus_acquired_cb,
				      name_acquired_cb,
				      name_lost_cb,
				      NULL, /* user_data */
				      NULL /* user_data_free_func */);

        g_main_loop_run (loop);

        STOP (manager);

        g_object_unref (manager);
        g_bus_unown_name (name_own_id);

        return 0;
}
