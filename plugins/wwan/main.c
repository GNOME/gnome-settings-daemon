#include "gsd-main-helper.h"
#include "gsd-wwan-manager.h"

int
main (int argc, char **argv)
{
        return gsd_main_helper (GSD_TYPE_WWAN_MANAGER, argc, argv);
}
