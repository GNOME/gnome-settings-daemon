/* Adapted from
 * http://gitorious.org/sensorfw/sensorfw/blobs/master/tests/contextfw/orientation/testorientation.py
 */

#include "config.h"

#include <glib-object.h>

#include "gsd-orientation-calc.h"

struct {
	int x;
	int y;
	int z;
	OrientationUp o;
} tests[] = {
	{ 60, 960, 18, ORIENTATION_NORMAL },
	{ 936, 162, 180, ORIENTATION_RIGHT_UP },
	{ 72, -990, -162, ORIENTATION_BOTTOM_UP },
	{ -954, -90, -36, ORIENTATION_LEFT_UP }
};

static void
test_no_prev (void)
{
	guint i;

	for (i = 0 ; i < G_N_ELEMENTS (tests); i++) {
		OrientationUp calculated;
		const char *got, *expected;

		calculated = gsd_orientation_calc (ORIENTATION_UNDEFINED, tests[i].x, tests[i].y, tests[i].z);
		got = gsd_orientation_to_string (calculated);
		expected = gsd_orientation_to_string (tests[i].o);

		g_assert_cmpstr (got, ==, expected);
	}
}

int main (int argc, char **argv)
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/show_bug.cgi?id=");

	g_test_add_func ("/orientation/no-prev", test_no_prev);
	return g_test_run ();
}
