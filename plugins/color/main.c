#include <gtk/gtk.h>

#include "gsd-main-helper.h"
#include "gsd-color-manager.h"

int
main (int argc, char **argv)
{
        gtk_init ();

        return gsd_main_helper (GSD_TYPE_COLOR_MANAGER, argc, argv);
}

