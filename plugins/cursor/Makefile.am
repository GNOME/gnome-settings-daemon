plugin_name = cursor

plugin_LTLIBRARIES = \
	libcursor.la

libcursor_la_SOURCES =		\
	gsd-cursor-manager.c	\
	gsd-cursor-manager.h	\
	gsd-cursor-plugin.c

libcursor_la_CPPFLAGS = \
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common/				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(AM_CPPFLAGS)

libcursor_la_CFLAGS = \
	$(PLUGIN_CFLAGS)		\
	$(CURSOR_CFLAGS)		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

libcursor_la_LDFLAGS =		\
	$(GSD_PLUGIN_LDFLAGS)

libcursor_la_LIBADD  =					\
	$(top_builddir)/plugins/common/libcommon.la	\
	$(CURSOR_LIBS)					\
	$(SETTINGS_PLUGIN_LIBS)

libexec_PROGRAMS = gsd-test-cursor

gsd_test_cursor_SOURCES =		\
	test-cursor.c			\
	gsd-cursor-manager.c		\
	gsd-cursor-manager.h

gsd_test_cursor_CPPFLAGS =					\
	-I$(top_srcdir)/data/					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	$(AM_CPPFLAGS)

gsd_test_cursor_CFLAGS =			\
	$(PLUGIN_CFLAGS)		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(CURSOR_CFLAGS)		\
	$(AM_CFLAGS)

gsd_test_cursor_LDADD =						\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(SETTINGS_DAEMON_LIBS)			\
	$(SETTINGS_PLUGIN_LIBS)			\
	$(CURSOR_LIBS)			\
	-lm

plugin_in_files = 		\
	cursor.gnome-settings-plugin.in

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = 			\
	$(plugin_in_files)

CLEANFILES = 			\
	$(plugin_DATA)

DISTCLEANFILES =		\
	$(plugin_DATA)

@GSD_INTLTOOL_PLUGIN_RULE@

