plugin_name = remote-display

plugin_LTLIBRARIES = libremote-display.la

libremote_display_la_SOURCES =	\
	gsd-remote-display-manager.c	\
	gsd-remote-display-manager.h	\
	gsd-remote-display-plugin.c

libremote_display_la_CPPFLAGS =				\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(AM_CPPFLAGS)

libremote_display_la_CFLAGS =	\
	$(PLUGIN_CFLAGS)		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

libremote_display_la_LDFLAGS = $(GSD_PLUGIN_LDFLAGS)

libremote_display_la_LIBADD  = $(SETTINGS_PLUGIN_LIBS)

libexec_PROGRAMS = gsd-test-remote-display

gsd_test_remote_display_SOURCES =		\
	test-remote-display.c			\
	gsd-remote-display-manager.c		\
	gsd-remote-display-manager.h

gsd_test_remote_display_CPPFLAGS =					\
	-I$(top_srcdir)/data/					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	$(AM_CPPFLAGS)

gsd_test_remote_display_CFLAGS =			\
	$(PLUGIN_CFLAGS)		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

gsd_test_remote_display_LDADD =						\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(SETTINGS_DAEMON_LIBS)			\
	$(SETTINGS_PLUGIN_LIBS)

plugin_in_files = remote-display.gnome-settings-plugin.in

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = $(plugin_in_files)

CLEANFILES = $(plugin_DATA)

DISTCLEANFILES = $(plugin_DATA)

@GSD_INTLTOOL_PLUGIN_RULE@
