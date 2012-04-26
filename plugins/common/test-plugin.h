/**
 * Create a test app for your plugin quickly.
 *
 * #define NEW gsd_media_keys_manager_new
 * #define START gsd_media_keys_manager_start
 * #define MANAGER GsdMediaKeysManager
 * #include "gsd-media-keys-manager.h"
 *
 * #include "test-plugin.h"
 */

#include "config.h"

#include <stdlib.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

static MANAGER *manager = NULL;

int
main (int argc, char **argv)
{
        GError  *error;

        bindtextdomain (GETTEXT_PACKAGE, GNOME_SETTINGS_LOCALEDIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

        error = NULL;
        if (! gtk_init_with_args (&argc, &argv, NULL, NULL, NULL, &error)) {
                fprintf (stderr, "%s", error->message);
                g_error_free (error);
                exit (1);
        }

        manager = NEW ();

        error = NULL;
        START (manager, &error);

        gtk_main ();

        return 0;
}
