#include "gsd-keyboard-manager.c"

int
main (void)
{
        gint i;
        const gchar *test_strings[][2] = {
                /* input                output */
                { "xkb:aa:bb:cc",       "aa+bb" },
                { "xkb:aa:bb:",         "aa+bb" },
                { "xkb:aa::cc",         "aa" },
                { "xkb:aa::",           "aa" },
                { "xkb::bb:cc",         "+bb" },
                { "xkb::bb:",           "+bb" },
                { "xkb:::cc",           "" },
                { "xkb:::",             "" },
        };

        for (i = 0; i < G_N_ELEMENTS (test_strings); ++i)
                g_assert_cmpstr (make_xkb_source_id (test_strings[i][0]), ==, test_strings[i][1]);

        return 0;
}
