#include "gsd-main-helper.h"
#include "gsd-smartcard-manager.h"

int
main (int argc, char **argv)
{
        return gsd_main_helper (GSD_TYPE_SMARTCARD_MANAGER, argc, argv);
}
