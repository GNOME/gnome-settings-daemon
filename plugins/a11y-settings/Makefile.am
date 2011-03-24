plugin_name = ally-settings

plugin_LTLIBRARIES = \
	liba11y-settings.la

liba11y_settings_la_SOURCES = 		\
	gsd-a11y-settings-manager.c	\
	gsd-a11y-settings-manager.h	\
	gsd-a11y-settings-plugin.c	\
	gsd-a11y-settings-plugin.h

liba11y_settings_la_CPPFLAGS = \
	-I$(top_srcdir)/gnome-settings-daemon		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	$(AM_CPPFLAGS)

liba11y_settings_la_CFLAGS = \
	$(PLUGIN_CFLAGS)		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

liba11y_settings_la_LDFLAGS = 		\
	$(GSD_PLUGIN_LDFLAGS)

liba11y_settings_la_LIBADD  = 		\
	$(SETTINGS_PLUGIN_LIBS)

plugin_in_files = 		\
	a11y-settings.gnome-settings-plugin.in

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = 			\
	$(plugin_in_files)

CLEANFILES = 			\
	$(plugin_DATA)

DISTCLEANFILES =		\
	$(plugin_DATA)

@GSD_INTLTOOL_PLUGIN_RULE@

