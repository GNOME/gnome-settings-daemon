plugin_name = ibus

plugin_LTLIBRARIES = \
	libibus.la

libibus_la_SOURCES = 		\
	gsd-ibus-manager.c	\
	gsd-ibus-manager.h	\
	gsd-ibus-plugin.c	\
	gsd-ibus-plugin.h

libibus_la_CPPFLAGS = \
	-I$(top_srcdir)/gnome-settings-daemon		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	$(AM_CPPFLAGS)

libibus_la_CFLAGS = \
	$(IBUS_CFLAGS) \
	$(PLUGIN_CFLAGS)		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

libibus_la_LDFLAGS = 		\
	$(GSD_PLUGIN_LDFLAGS)

libibus_la_LIBADD  = 		\
	$(IBUS_LIBS)	\
	$(SETTINGS_PLUGIN_LIBS)

plugin_in_files = 		\
	ibus.gnome-settings-plugin.in

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = 			\
	$(plugin_in_files)

CLEANFILES = 			\
	$(plugin_DATA)

DISTCLEANFILES =		\
	$(plugin_DATA)

@GSD_INTLTOOL_PLUGIN_RULE@
