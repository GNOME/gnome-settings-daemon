plugin_LTLIBRARIES = \
	libxrandr.la

libxrandr_la_SOURCES = 		\
	gsd-xrandr-plugin.h	\
	gsd-xrandr-plugin.c	\
	gsd-xrandr-manager.h	\
	gsd-xrandr-manager.c

libxrandr_la_CPPFLAGS = \
	-I$(top_srcdir)/gnome-settings-daemon		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	$(AM_CPPFLAGS)

libxrandr_la_CFLAGS = \
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

libxrandr_la_LDFLAGS = 		\
	$(GSD_PLUGIN_LDFLAGS)

libxrandr_la_LIBADD  = 		\
	$(SETTINGS_PLUGIN_LIBS)	\
	$(RANDR_LIBS)

plugin_in_files = 		\
	xrandr.gnome-settings-plugin.in

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = $(plugin_in_files)
CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)

@GSD_INTLTOOL_PLUGIN_RULE@
