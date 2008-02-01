NULL =

plugin_LTLIBRARIES = \
	libmouse.la		\
	$(NULL)

libmouse_la_SOURCES = 		\
	gsd-mouse-plugin.h	\
	gsd-mouse-plugin.c	\
	gsd-mouse-manager.h	\
	gsd-mouse-manager.c	\
	gsd-locate-pointer.h	\
	gsd-locate-pointer.c	\
	gsd-timeline.h		\
	gsd-timeline.c		\
	$(NULL)

libmouse_la_CPPFLAGS = \
	-I$(top_srcdir)/src		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	$(AM_CPPFLAGS)

libmouse_la_CFLAGS = \
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

libmouse_la_LDFLAGS = 		\
	$(GSD_PLUGIN_LDFLAGS)	\
	$(NULL)

libmouse_la_LIBADD  = 		\
	$(SETTINGS_PLUGIN_LIBS)	\
	$(NULL)

plugin_in_files = 		\
	mouse.gnome-settings-plugin.in	\
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
