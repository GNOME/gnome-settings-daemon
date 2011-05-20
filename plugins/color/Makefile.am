plugin_name = color

plugin_LTLIBRARIES = \
	libcolor.la

libcolor_la_SOURCES = 		\
	gsd-color-manager.c	\
	gsd-color-manager.h	\
	gsd-color-plugin.c	\
	gsd-color-plugin.h

libcolor_la_CPPFLAGS = \
	-I$(top_srcdir)/gnome-settings-daemon		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	-DBINDIR=\"$(bindir)\"				\
	$(AM_CPPFLAGS)

libcolor_la_CFLAGS = \
	$(PLUGIN_CFLAGS)		\
	$(COLORD_CFLAGS)		\
	$(LIBCANBERRA_CFLAGS)		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

libcolor_la_LDFLAGS = 		\
	$(GSD_PLUGIN_LDFLAGS)

libcolor_la_LIBADD  = 		\
	$(COLORD_LIBS)		\
	$(LIBCANBERRA_LIBS)	\
	$(SETTINGS_PLUGIN_LIBS)

plugin_in_files = 		\
	color.gnome-settings-plugin.in

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = 			\
	$(plugin_in_files)

CLEANFILES = 			\
	$(plugin_DATA)

DISTCLEANFILES =		\
	$(plugin_DATA)

@GSD_INTLTOOL_PLUGIN_RULE@
