plugin_name = orientation

noinst_PROGRAMS = test-orientation-calculation

test_orientation_calculation_SOURCES =	\
	gsd-orientation-calc.c		\
	gsd-orientation-calc.h		\
	test-orientation-calculation.c

test_orientation_calculation_CFLAGS =	\
	$(PLUGIN_CFLAGS)		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

test_orientation_calculation_LDADD =	\
	$(SETTINGS_PLUGIN_LIBS)

plugin_LTLIBRARIES = liborientation.la

liborientation_la_SOURCES = 		\
	gsd-orientation-plugin.h	\
	gsd-orientation-plugin.c	\
	gsd-orientation-manager.h	\
	gsd-orientation-manager.c	\
	gsd-orientation-calc.c		\
	gsd-orientation-calc.h

liborientation_la_CPPFLAGS = \
	-I$(top_srcdir)/gnome-settings-daemon		\
	-I$(top_srcdir)/plugins/common/			\
	-I$(top_srcdir)/data/				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	-DLIBEXECDIR=\""$(libexecdir)"\" 		\
	$(AM_CPPFLAGS)

liborientation_la_CFLAGS = \
	$(PLUGIN_CFLAGS)		\
	$(GUDEV_CFLAGS)			\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

liborientation_la_LDFLAGS = 		\
	$(GSD_PLUGIN_LDFLAGS)

liborientation_la_LIBADD  = 							\
	$(top_builddir)/plugins/common/libcommon.la			\
	$(SETTINGS_PLUGIN_LIBS)						\
	$(X11_LIBS)							\
	$(XINPUT_LIBS)

plugin_in_files = orientation.gnome-settings-plugin.in

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = $(plugin_in_files)
CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)

@GSD_INTLTOOL_PLUGIN_RULE@
