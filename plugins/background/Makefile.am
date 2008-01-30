NULL =

SUBDIRS =		\
	libbackground	\
	$(NULL)

modules_flags = -export_dynamic -avoid-version -module -no-undefined

plugindir = $(libdir)/gnome-settings-daemon/plugins/background

INCLUDES = \
	-I$(top_srcdir)/src		\
	-I$(top_srcdir)/plugins/background/libbackground	\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(SETTINGS_PLUGIN_CFLAGS)				\
	$(NULL)

noinst_PROGRAMS = 			\
	test-background			\
	$(NULL)

test_background_SOURCES = 		\
	test-background.c		\
	gsd-background-manager.h	\
	gsd-background-manager.c	\
	$(NULL)

test_background_LDADD =		\
	$(SETTINGS_PLUGIN_LIBS)	\
	$(top_builddir)/plugins/background/libbackground/libbackground.la		\
	$(NULL)

plugin_LTLIBRARIES = \
	libbackground.la		\
	$(NULL)

libbackground_la_SOURCES = 		\
	gsd-background-plugin.h	\
	gsd-background-plugin.c	\
	gsd-background-manager.h	\
	gsd-background-manager.c	\
	$(NULL)

libbackground_la_LDFLAGS = 		\
	$(PLUGIN_LIBTOOL_FLAGS)	\
	$(modules_flags)	\
	$(NULL)

libbackground_la_LIBADD  = 		\
	$(SETTINGS_PLUGIN_LIBS)		\
	$(top_builddir)/plugins/background/libbackground/libbackground.la		\
	$(NULL)

libbackground_la_CFLAGS = 		\
	$(NULL)

plugin_in_files = 		\
	background.gnome-settings-plugin.in	\
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
