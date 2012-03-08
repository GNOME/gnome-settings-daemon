#include "gsd-xsettings-manager.h"
#include <gtk/gtk.h>

int
main (void)
{
  GnomeXSettingsManager *manager;
  GError *error = NULL;

  gtk_init (NULL, NULL);

  manager = gnome_xsettings_manager_new ();
  gnome_xsettings_manager_start (manager, &error);
  g_assert_no_error (error);

  gtk_main ();

  return 0;
}
