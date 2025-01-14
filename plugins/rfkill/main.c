#include "gsd-main-helper.h"
#include "gsd-rfkill-manager.h"

int
main (int argc, char **argv)
{
        return gsd_main_helper (GSD_TYPE_RFKILL_MANAGER, argc, argv);
}
