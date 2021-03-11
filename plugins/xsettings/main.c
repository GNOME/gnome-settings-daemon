#define NEW gsd_xsettings_manager_new
#define START gsd_xsettings_manager_start
#define STOP gsd_xsettings_manager_stop
#define MANAGER GsdXSettingsManager
#define GDK_BACKEND "x11"
#define MAIN_HOOK main_hook

#include <glib.h>

static void main_hook (void)
{
	/* Do not try to run if X11 is not available */
	if (g_getenv ("DISPLAY") == NULL)
		exit (0);
}

#include "gsd-xsettings-manager.h"

#include "daemon-skeleton-gtk.h"
