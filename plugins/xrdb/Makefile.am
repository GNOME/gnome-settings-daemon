NULL =

SUBDIRS =		\
	data		\
	$(NULL)

modules_flags = -export_dynamic -avoid-version -module -no-undefined

plugindir = $(libdir)/gnome-settings-daemon/plugins/xrdb

INCLUDES = \
	-I$(top_srcdir)/src		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	-DDATADIR=\""$(datadir)/gnome-settings-daemon"\"	\
	$(SETTINGS_PLUGIN_CFLAGS)				\
	$(NULL)

plugin_LTLIBRARIES = \
	libxrdb.la		\
	$(NULL)

libxrdb_la_SOURCES = 		\
	gsd-xrdb-plugin.h	\
	gsd-xrdb-plugin.c	\
	gsd-xrdb-manager.h	\
	gsd-xrdb-manager.c	\
	$(NULL)

libxrdb_la_LDFLAGS = 		\
	$(PLUGIN_LIBTOOL_FLAGS)	\
	$(modules_flags)	\
	$(NULL)

libxrdb_la_LIBADD  = 		\
	$(SETTINGS_PLUGIN_LIBS)	\
	$(NULL)

libxrdb_la_CFLAGS = 		\
	$(NULL)

plugin_in_files = 		\
	xrdb.gnome-settings-plugin.in	\
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
