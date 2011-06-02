#include <gtk/gtk.h>
#include "eggaccelerators.h"

#define KEY "XF86AudioRepeat"

int main (int argc, char **argv)
{
	guint accel_key;
	guint *accel_codes;
	EggVirtualModifierType mods;
	EggParseError retval;

	gtk_init (&argc, &argv);

	g_message ("gdk_keyval_from_name ('%s') == %d", KEY, gdk_keyval_from_name(KEY));
	retval = egg_accelerator_parse_virtual (KEY, &accel_key, &accel_codes, &mods);
	g_message ("egg_accelerator_parse_virtual ('%s') returned '%d' (%s)", KEY, retval,
		   (retval == EGG_PARSE_ERROR_NONE) ? "success" : "failure");

	return 0;
}
