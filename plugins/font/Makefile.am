NULL =

modules_flags = -export_dynamic -avoid-version -module -no-undefined

plugindir = $(libdir)/gnome-settings-daemon/plugins/font

INCLUDES = \
	-I$(top_srcdir)/src		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"\
	$(SETTINGS_PLUGIN_CFLAGS)			\
	$(NULL)

plugin_LTLIBRARIES = \
	libfont.la		\
	$(NULL)

libfont_la_SOURCES = 		\
	gsd-font-plugin.h	\
	gsd-font-plugin.c	\
	gsd-font-manager.h	\
	gsd-font-manager.c	\
	delayed-dialog.h	\
	delayed-dialog.c	\
	$(NULL)

libfont_la_LDFLAGS = 		\
	$(PLUGIN_LIBTOOL_FLAGS)	\
	$(modules_flags)	\
	$(NULL)

libfont_la_LIBADD  = 		\
	$(SETTINGS_PLUGIN_LIBS)	\
	$(NULL)

libfont_la_CFLAGS = 		\
	$(NULL)

plugin_in_files = 		\
	font.gnome-settings-plugin.in	\
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
