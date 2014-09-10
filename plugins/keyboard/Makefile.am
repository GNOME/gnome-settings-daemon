NULL =

plugin_name = keyboard

plugin_LTLIBRARIES = \
	libkeyboard.la		\
	$(NULL)

libkeyboard_la_SOURCES = 	\
	gsd-keyboard-plugin.c	\
	gsd-keyboard-manager.h	\
	gsd-keyboard-manager.c	\
	$(NULL)

libkeyboard_la_CPPFLAGS = \
	-I$(top_srcdir)/gnome-settings-daemon		\
	-I$(top_builddir)/gnome-settings-daemon		\
	-I$(top_srcdir)/data				\
	-I$(top_srcdir)/plugins/common			\
	-DDATADIR=\""$(pkgdatadir)"\"			\
	-DLIBEXECDIR=\""$(libexecdir)"\"		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	$(AM_CPPFLAGS)

libkeyboard_la_CFLAGS = \
	$(PLUGIN_CFLAGS)		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

libkeyboard_la_LDFLAGS = 	\
	$(GSD_PLUGIN_LDFLAGS)	\
	$(NULL)

libkeyboard_la_LIBADD  =				\
	$(top_builddir)/plugins/common/libcommon.la	\
	$(SETTINGS_PLUGIN_LIBS)				\
	$(NULL)

libexec_PROGRAMS = gsd-test-keyboard
gsd_test_keyboard_SOURCES =	\
	test-keyboard.c		\
	gsd-keyboard-manager.h	\
	gsd-keyboard-manager.c	\
	$(NULL)

gsd_test_keyboard_CFLAGS = $(libkeyboard_la_CFLAGS)
gsd_test_keyboard_CPPFLAGS = $(libkeyboard_la_CPPFLAGS)
gsd_test_keyboard_LDADD = $(libkeyboard_la_LIBADD) $(top_builddir)/gnome-settings-daemon/libgsd.la

plugin_in_files = 		\
	keyboard.gnome-settings-plugin.in	\
	$(NULL)

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = 			\
	$(plugin_in_files)	\
	$(NULL)

CLEANFILES = 			\
	$(plugin_DATA)		\
	$(NULL)

DISTCLEANFILES =		\
	$(plugin_DATA)		\
	$(NULL)

@GSD_INTLTOOL_PLUGIN_RULE@
