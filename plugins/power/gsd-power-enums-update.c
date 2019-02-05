#include <glib-object.h>
#include <gsd-power-enums.h>

static void
output_enum_values (GType class_type)
{
	GEnumClass *eclass;
	guint i;

	eclass = G_ENUM_CLASS (g_type_class_peek (class_type));
	for (i = 0; i < eclass->n_values; i++) {
		GEnumValue *value = &(eclass->values[i]);
		printf ("%s = %d;\n", value->value_name, value->value);
	}
}

static void
output_flags_values (GType class_type)
{
	GFlagsClass *fclass;
	guint i;

	fclass = G_FLAGS_CLASS (g_type_class_peek (class_type));
	for (i = 0; i < fclass->n_values; i++) {
		GFlagsValue *value = &(fclass->values[i]);
		printf ("%s = %d;\n", value->value_name, value->value);
	}
}

int
main (int argc, char **argv)
{
	/* XXX: This is a bad workaround for the fact that the g_print calls
	 *      cause some checks to fail when running the clang memory
	 *      sanitizer.
	 */
	//g_setenv ("CHARSET", "UTF-8", TRUE);

	g_type_class_ref (GSD_POWER_TYPE_INHIBITOR_FLAG);
	g_type_class_ref (GSD_POWER_TYPE_PRESENCE_STATUS);
	output_flags_values (GSD_POWER_TYPE_INHIBITOR_FLAG);
	output_enum_values (GSD_POWER_TYPE_PRESENCE_STATUS);
	return 0;
}
