plugin_LTLIBRARIES = libmouse.la

libmouse_la_SOURCES = 		\
	gsd-mouse-plugin.h	\
	gsd-mouse-plugin.c	\
	gsd-mouse-manager.h	\
	gsd-mouse-manager.c	\
	gsd-locate-pointer.h	\
	gsd-locate-pointer.c	\
	gsd-timeline.h		\
	gsd-timeline.c

libmouse_la_CPPFLAGS = \
	-I$(top_srcdir)/gnome-settings-daemon		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	$(AM_CPPFLAGS)

libmouse_la_CFLAGS = \
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

libmouse_la_LDFLAGS = 		\
	$(GSD_PLUGIN_LDFLAGS)

libmouse_la_LIBADD  = 		\
	$(top_builddir)/gnome-settings-daemon/libgsd-plugins.la	\
	$(SETTINGS_PLUGIN_LIBS)	\
	$(XINPUT_LIBS)

plugin_in_files = mouse.gnome-settings-plugin.in

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = $(plugin_in_files)
CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)

@GSD_INTLTOOL_PLUGIN_RULE@
