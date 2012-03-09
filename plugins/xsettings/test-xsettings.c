#include "config.h"

#include "gsd-xsettings-manager.h"
#include <glib/gi18n.h>
#include <gtk/gtk.h>

int
main (void)
{
  GnomeXSettingsManager *manager;
  GError *error = NULL;

  bindtextdomain (GETTEXT_PACKAGE, GNOME_SETTINGS_LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  gtk_init (NULL, NULL);

  manager = gnome_xsettings_manager_new ();
  gnome_xsettings_manager_start (manager, &error);
  g_assert_no_error (error);

  gtk_main ();

  return 0;
}
