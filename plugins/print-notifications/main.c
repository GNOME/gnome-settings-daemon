#include <gtk/gtk.h>

#include "gsd-main-helper.h"
#include "gsd-print-notifications-manager.h"

int
main (int argc, char **argv)
{
        gtk_init ();

        return gsd_main_helper (GSD_TYPE_PRINT_NOTIFICATIONS_MANAGER, argc, argv);
}
