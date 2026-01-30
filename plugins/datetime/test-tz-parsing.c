#include <glib.h>
#include <locale.h>
#include <math.h>

/* Include directly to test static function 'convert_pos' */
#include "tz.c"

static void
test_convert_pos_simple (void)
{
	/* Paris */
	gdouble res = convert_pos ("+4852", 2);
	g_assert_cmpfloat_with_epsilon (res, 48.8666, 0.001);
}

static void
test_convert_pos_negative_simple (void)
{
	/* Rio */
	gdouble res = convert_pos ("-2254", 2);
	g_assert_cmpfloat_with_epsilon (res, -22.9, 0.001);
}

static void
test_convert_pos_negative_zero (void)
{
	/* Quito (-0 deg 13 min) - Regression test for negative zero logic */
	gdouble res = convert_pos ("-0013", 2);
	g_assert_cmpfloat_with_epsilon (res, -0.2166, 0.001);
}

static void
test_convert_pos_contiguous_nyc (void)
{
	/* NYC: "+4042-07400" - Must stop parsing at longitude sign */
	gdouble res = convert_pos ("+4042-07400", 2);
	g_assert_cmpfloat_with_epsilon (res, 40.7, 0.001);
}

static void
test_convert_pos_safety (void)
{
	g_assert_cmpfloat (convert_pos (NULL, 2), ==, 0.0);
	g_assert_cmpfloat (convert_pos ("", 2), ==, 0.0);
	g_assert_cmpfloat (convert_pos ("+12", 2), ==, 0.0);
	g_assert_cmpfloat (convert_pos ("+123", 10), ==, 0.0);
}

int
main (int argc, char **argv)
{
	setlocale (LC_ALL, "");
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/tz/parsing/simple", test_convert_pos_simple);
	g_test_add_func ("/tz/parsing/negative", test_convert_pos_negative_simple);
	g_test_add_func ("/tz/parsing/negative_zero", test_convert_pos_negative_zero);
	g_test_add_func ("/tz/parsing/contiguous_nyc", test_convert_pos_contiguous_nyc);
	g_test_add_func ("/tz/parsing/safety", test_convert_pos_safety);

	return g_test_run ();
}
