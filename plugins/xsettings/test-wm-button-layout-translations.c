#include <glib.h>

#include "wm-button-layout-translation.h"

static void
test_button_layout_translations (void)
{
  static struct {
    char *layout;
    char *expected;
  } tests[] = {
    { "", "" },
    { "invalid", "" },

    { ":", ":" },
    { ":invalid", ":" },
    { "invalid:", ":" },
    { "invalid:invalid", ":" },

    { "appmenu", "menu" },
    { "appmenu:", "menu:" },
    { ":menu", ":icon" },
    { "appmenu:close", "menu:close" },
    { "appmenu:minimize,maximize,close", "menu:minimize,maximize,close" },
    { "menu,appmenu:minimize,maximize,close", "icon,menu:minimize,maximize,close" },

    { "close,close,close:close,close,close", "close,close,close:close,close,close" },

    { "invalid,appmenu:invalid,minimize", "menu:minimize" },
    { "appmenu,invalid:minimize,invalid", "menu:minimize" },
    { "invalidmenu:invalidclose", ":" },
    { "invalid,invalid,invalid:invalid,minimize,maximize,close", ":minimize,maximize,close" },
  };
  int i;

  for (i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      char *layout = g_strdup (tests[i].layout);

      translate_wm_button_layout_to_gtk (layout);
      g_assert_cmpstr (layout, ==, tests[i].expected);
      g_free (layout);
    }
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/layout-translations", test_button_layout_translations);

  return g_test_run ();
}
