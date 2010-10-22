plugin_LTLIBRARIES = libgconf.la

libgconf_la_SOURCES =		\
	gsd-gconf-plugin.c	\
	gsd-gconf-plugin.h	\
	gsd-gconf-manager.c	\
	gsd-gconf-manager.h

libgconf_la_CPPFLAGS =						\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
        -DLIBEXECDIR=\""$(libexecdir)"\"			\
        $(AM_CPPFLAGS)

libgconf_la_CFLAGS =			\
	$(GCONF_CFLAGS)			\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

libgconf_la_LDFLAGS = $(GSD_PLUGIN_LDFLAGS)

libgconf_la_LIBADD =		\
	$(GCONF_LIBS)		\
	$(SETTINGS_PLUGIN_LIBS)

plugin_in_files = gconf.gnome-settings-plugin.in

plugins_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = $(plugin_in_files)
CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)

@GSD_INTLTOOL_PLUGIN_RULE@