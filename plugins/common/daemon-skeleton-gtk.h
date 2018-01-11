/**
 * Create a gnome-settings-daemon helper easily
 *
 * #define NEW gsd_media_keys_manager_new
 * #define START gsd_media_keys_manager_start
 * #define MANAGER GsdMediaKeysManager
 * #include "gsd-media-keys-manager.h"
 *
 * #include "daemon-skeleton-gtk.h"
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <locale.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gnome-settings-bus.h"

#ifndef PLUGIN_NAME
#error Include PLUGIN_CFLAGS in the daemon s CFLAGS
#endif /* !PLUGIN_NAME */

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

static const char *gdm_helpers[] = {
	"a11y-keyboard",
	"a11y-settings",
	"clipboard",
	"color",
	"keyboard",
	"media-keys",
	"power",
	"smartcard",
	"sound",
	"xsettings",
	"wacom",
};

static gboolean
should_run (void)
{
	const char *session_mode;
	guint i;

	session_mode = g_getenv ("GNOME_SHELL_SESSION_MODE");
	if (g_strcmp0 (session_mode, "gdm") != 0)
		return TRUE;

	for (i = 0; i < G_N_ELEMENTS (gdm_helpers); i++) {
		if (g_str_equal (PLUGIN_NAME, gdm_helpers[i]))
			return TRUE;
	}
	return FALSE;
}

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
do_stop (void)
{
        gtk_main_quit ();
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
                do_stop ();
        }
}

static void
on_client_registered (GObject             *source_object,
                      GAsyncResult        *res,
                      gpointer             user_data)
{
        GVariant *variant;
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
                          G_CALLBACK (client_proxy_signal_cb), NULL);

        g_free (object_path);
        g_variant_unref (variant);
}

static void
register_with_gnome_session (void)
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
			   NULL);
}

static void
set_empty_gtk_theme (gboolean set)
{
	static char *old_gtk_theme = NULL;

	if (set) {
		/* Override GTK_THEME to reduce overhead of CSS engine. By using
		 * GTK_THEME environment variable, GtkSettings is not allowed to
		 * initially parse the Adwaita theme.
		 *
		 * https://bugzilla.gnome.org/show_bug.cgi?id=780555 */
		old_gtk_theme = g_strdup (g_getenv ("GTK_THEME"));
		g_setenv ("GTK_THEME", "Disabled", TRUE);
	} else {
		/* GtkSettings has loaded, so we can drop GTK_THEME used to initialize
		 * our internal theme. Only the main thread accesses the GTK_THEME
		 * environment variable, so this is safe to release. */
		if (old_gtk_theme != NULL)
			g_setenv ("GTK_THEME", old_gtk_theme, TRUE);
		else
			g_unsetenv ("GTK_THEME");
	}
}

int
main (int argc, char **argv)
{
        GError  *error;

        bindtextdomain (GETTEXT_PACKAGE, GNOME_SETTINGS_LOCALEDIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);
        setlocale (LC_ALL, "");

        set_empty_gtk_theme (TRUE);

        gdk_set_allowed_backends ("x11");

        error = NULL;
        if (! gtk_init_with_args (&argc, &argv, PLUGIN_NAME, entries, NULL, &error)) {
                if (error != NULL) {
                        fprintf (stderr, "%s\n", error->message);
                        g_error_free (error);
                }
                exit (1);
        }

        set_empty_gtk_theme (FALSE);

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
		id = g_timeout_add_seconds (timeout, (GSourceFunc) gtk_main_quit, NULL);
		g_source_set_name_by_id (id, "[gnome-settings-daemon] gtk_main_quit");
	}

        manager = NEW ();
	register_with_gnome_session ();

	if (should_run ()) {
		error = NULL;
		if (!START (manager, &error)) {
			fprintf (stderr, "Failed to start: %s\n", error->message);
			g_error_free (error);
			exit (1);
		}
	}

        gtk_main ();

	if (should_run ())
		STOP (manager);
        g_object_unref (manager);

        return 0;
}
