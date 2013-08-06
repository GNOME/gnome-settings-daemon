NULL =

plugin_name = a11y-keyboard

libexec_PROGRAMS = gsd-test-a11y-keyboard

gsd_test_a11y_keyboard_SOURCES =		\
	gsd-a11y-keyboard-manager.h		\
	gsd-a11y-keyboard-manager.c		\
	test-a11y-keyboard.c

gsd_test_a11y_keyboard_CFLAGS =					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	-DGTKBUILDERDIR=\""$(pkgdatadir)"\"			\
	$(PLUGIN_CFLAGS)					\
	$(LIBNOTIFY_CFLAGS)					\
	$(SETTINGS_PLUGIN_CFLAGS)				\
	$(AM_CFLAGS)

gsd_test_a11y_keyboard_LDADD =					\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(LIBNOTIFY_LIBS)					\
	$(SETTINGS_PLUGIN_LIBS)

plugin_LTLIBRARIES = \
	liba11y-keyboard.la		\
	$(NULL)

liba11y_keyboard_la_SOURCES = 		\
	gsd-a11y-keyboard-plugin.c	\
	gsd-a11y-keyboard-manager.h	\
	gsd-a11y-keyboard-manager.c	\
	$(NULL)

liba11y_keyboard_la_CPPFLAGS = \
	-I$(top_srcdir)/gnome-settings-daemon		\
	-I$(top_builddir)/gnome-settings-daemon		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	-DGTKBUILDERDIR=\""$(gtkbuilderdir)"\" \
	$(AM_CPPFLAGS)

liba11y_keyboard_la_CFLAGS = \
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(LIBNOTIFY_CFLAGS)		\
	$(AM_CFLAGS)

liba11y_keyboard_la_LDFLAGS = 		\
	$(GSD_PLUGIN_LDFLAGS)		\
	$(NULL)

liba11y_keyboard_la_LIBADD  = 		\
	$(SETTINGS_PLUGIN_LIBS)		\
	$(XF86MISC_LIBS)		\
	$(LIBNOTIFY_LIBS)		\
	$(NULL)

plugin_in_files = 		\
	a11y-keyboard.gnome-settings-plugin.in	\
	$(NULL)

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = 			\
	$(plugin_in_files)	\
	$(gtkbuilder_DATA)		\
	$(NULL)

CLEANFILES = 			\
	$(plugin_DATA)		\
	$(NULL)

DISTCLEANFILES =		\
	$(plugin_DATA)		\
	$(NULL)

@GSD_INTLTOOL_PLUGIN_RULE@
