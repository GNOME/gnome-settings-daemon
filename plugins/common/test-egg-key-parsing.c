#include <gtk/gtk.h>
#include "eggaccelerators.h"

#define KEY "<Alt>XF86AudioMute"

int main (int argc, char **argv)
{
	guint gdk_accel_key, egg_accel_key;
	guint *gdk_accel_codes, *egg_accel_codes;
	GdkModifierType gdk_mods;
	EggVirtualModifierType egg_mods;
	EggParseError retval;

	gtk_init (&argc, &argv);

	g_message ("gdk_keyval_from_name ('%s') == %d", KEY, gdk_keyval_from_name(KEY));

	retval = egg_accelerator_parse_virtual (KEY, &egg_accel_key, &egg_accel_codes, &egg_mods);
	g_message ("egg_accelerator_parse_virtual ('%s') returned keyval '%d' keycode[0]: '%d' mods: 0x%x (%s)",
		   KEY, egg_accel_key, egg_accel_codes ? egg_accel_codes[0] : 0, egg_mods,
		   (retval == EGG_PARSE_ERROR_NONE) ? "success" : "failure");

	gtk_accelerator_parse_with_keycode (KEY, &gdk_accel_key, &gdk_accel_codes, &gdk_mods);
	g_message ("gtk_accelerator_parse_full ('%s') returned keyval '%d' keycode[0]: '%d' mods: 0x%x",
		   KEY, gdk_accel_key, gdk_accel_codes ? gdk_accel_codes[0] : 0, gdk_mods);

	return 0;
}
