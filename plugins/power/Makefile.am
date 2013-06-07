BUILT_SOURCES =

plugin_name = power

plugin_LTLIBRARIES =					\
	libpower.la

libpower_la_SOURCES = 					\
	gpm-common.c					\
	gpm-common.h					\
	gsd-backlight-linux.c				\
	gsd-backlight-linux.h				\
	gsd-power-manager.c				\
	gsd-power-manager.h				\
	gsm-inhibitor-flag.h				\
	gsm-presence-flag.h				\
	gsm-manager-logout-mode.h			\
	gsd-power-constants.h				\
	gsd-power-plugin.c

libpower_la_CPPFLAGS = \
	-I$(top_srcdir)/data/				\
	-I$(top_srcdir)/gnome-settings-daemon		\
	-I$(top_srcdir)/plugins/common			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	-DGTKBUILDERDIR=\""$(pkgdatadir)"\"		\
	-DSBINDIR=\"$(sbindir)\"			\
	-DLIBEXECDIR=\"$(libexecdir)\"			\
	$(AM_CPPFLAGS)

libpower_la_CFLAGS = \
	$(PLUGIN_CFLAGS)				\
	$(POWER_CFLAGS)					\
	$(SETTINGS_PLUGIN_CFLAGS)			\
	$(AM_CFLAGS)

libpower_la_LDFLAGS = 					\
	$(GSD_PLUGIN_LDFLAGS)

libpower_la_LIBADD  = 					\
	$(top_builddir)/plugins/common/libcommon.la	\
	$(top_builddir)/gnome-settings-daemon/libgsd.la \
	$(POWER_LIBS)					\
	$(SETTINGS_PLUGIN_LIBS)

plugin_in_files = 					\
	power.gnome-settings-plugin.in

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

org.gnome.settings-daemon.plugins.power.policy.in: org.gnome.settings-daemon.plugins.power.policy.in.in Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

@INTLTOOL_POLICY_RULE@
polkit_policydir = $(datadir)/polkit-1/actions
polkit_policy_in_files = org.gnome.settings-daemon.plugins.power.policy.in
polkit_policy_DATA = $(polkit_policy_in_files:.policy.in=.policy)

# so it always gets included in the tarball
gsd_backlight_helper_SOURCES =				\
	gsd-backlight-linux.c				\
	gsd-backlight-linux.h				\
	gsd-backlight-helper.c

noinst_PROGRAMS = gsd-test-power
gsd_test_power_SOURCES =				\
	gpm-common.c					\
	gpm-common.h					\
	gsd-backlight-linux.c				\
	gsd-backlight-linux.h				\
	gsd-power-manager.c				\
	gsd-power-manager.h				\
	gsm-inhibitor-flag.h				\
	gsm-presence-flag.h				\
	gsm-manager-logout-mode.h			\
	test-power.c

gsd_test_power_CFLAGS = $(libpower_la_CFLAGS)
gsd_test_power_CPPFLAGS = $(libpower_la_CPPFLAGS) -DGSD_MOCK=1 -DGSD_ACTION_DELAY=1
gsd_test_power_LDADD =					\
	-lm						\
	$(top_builddir)/plugins/common/libcommon.la	\
	$(top_builddir)/gnome-settings-daemon/libgsd.la \
	$(POWER_LIBS)					\
	$(SETTINGS_PLUGIN_LIBS)

EXTRA_DIST = 						\
	$(plugin_in_files)				\
	$(gsd_backlight_helper_SOURCES)			\
	$(NULL)

if HAVE_GUDEV
libexec_PROGRAMS = gsd-backlight-helper

gsd_backlight_helper_LDFLAGS =				\
	$(BACKLIGHT_HELPER_LIBS)			\
	-lm

gsd_backlight_helper_CFLAGS =				\
	$(BACKLIGHT_HELPER_CFLAGS)

EXTRA_DIST += 						\
	org.gnome.settings-daemon.plugins.power.policy.in.in
endif

# Enums
GSD_POWER_ENUM_FILES = gsd-power-enums.c gsd-power-enums.h

gsd-power-enums.h: gsm-inhibitor-flag.h gsm-presence-flag.h Makefile
	$(AM_V_GEN)($(GLIB_MKENUMS) \
			--fhead "#ifndef GSD_POWER_ENUMS_H\n#define GSD_POWER_ENUMS_H\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
			--fprod "/* enumerations from \"@filename@\" */\n" \
			--vhead "GType @enum_name@_get_type (void) G_GNUC_CONST;\n#define GSD_POWER_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n" \
			--ftail "G_END_DECLS\n\n#endif /* !GSD_POWER_ENUMS_H */" $(srcdir)/gsm-inhibitor-flag.h $(srcdir)/gsm-presence-flag.h > $@)

gsd-power-enums.c: gsd-power-constants.h gsm-inhibitor-flag.h gsm-presence-flag.h Makefile gsd-power-enums.h
	$(AM_V_GEN)($(GLIB_MKENUMS) \
			--fhead "#include \"gsm-inhibitor-flag.h\"\n#include \"gsm-presence-flag.h\"\n#include \"gsd-power-enums.h\"" \
			--fprod "\n/* enumerations from \"@filename@\" */" \
			--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {" \
			--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
			--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
		$(srcdir)/gsm-inhibitor-flag.h $(srcdir)/gsm-presence-flag.h > $@)

BUILT_SOURCES += $(GSD_POWER_ENUM_FILES)

gsdpowerconstants.py: gsd-power-constants-update.pl gsd-power-constants.h
	$(AM_V_GEN) $(srcdir)/gsd-power-constants-update.pl

gsdpowerenums.py: gsd-power-enums-update gsd-power-enums.h gsd-power-enums.c
	$(AM_V_GEN) $(builddir)/gsd-power-enums-update > $@

noinst_PROGRAMS += gsd-power-enums-update
gsd_power_enums_update_SOURCES =		\
	gsd-power-enums-update.c		\
	gsd-power-enums.h			\
	gsd-power-enums.c
gsd_power_enums_update_CFLAGS = $(libpower_la_CFLAGS)
gsd_power_enums_update_CPPFLAGS = $(libpower_la_CPPFLAGS) -I$(top_builddir)/plugins/power
gsd_power_enums_update_LDFLAGS = $(POWER_LIBS)

EXTRA_DIST += gsd-power-constants-update.pl gsdpowerconstants.py gsdpowerenums.py test.py

check-local: $(top_builddir)/tests/shiftkey gsd-test-power test.py gsdpowerconstants.py gsdpowerenums.py
# This is how you run a single test
#	BUILDDIR=$(builddir) TOP_BUILDDIR=$(top_builddir) ${PYTHON} $(srcdir)/test.py PowerPluginTest.test_sleep_inactive_blank
	BUILDDIR=$(builddir) TOP_BUILDDIR=$(top_builddir) ${PYTHON} $(srcdir)/test.py

clean-local:
	rm -f *~

CLEANFILES = 						\
	$(plugin_DATA)					\
	$(GSD_POWER_ENUM_FILES)				\
	gsdpowerenums.py				\
	org.gnome.settings-daemon.plugins.power.policy	\
	org.gnome.settings-daemon.plugins.power.policy.in

@GSD_INTLTOOL_PLUGIN_RULE@
