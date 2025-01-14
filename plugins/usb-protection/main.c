#include "gsd-main-helper.h"
#include "gsd-usb-protection-manager.h"

int
main (int argc, char **argv)
{
        return gsd_main_helper (GSD_TYPE_USB_PROTECTION_MANAGER, argc, argv);
}
