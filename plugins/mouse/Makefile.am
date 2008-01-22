NULL =

modules_flags = -export_dynamic -avoid-version -module -no-undefined

plugindir = $(libdir)/gnome-settings-daemon/plugins/mouse

INCLUDES = \
	-I$(top_srcdir)/src		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"\
	$(SETTINGS_PLUGIN_CFLAGS)			\
	$(NULL)

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

libmouse_la_LDFLAGS = 		\
	$(PLUGIN_LIBTOOL_FLAGS)	\
	$(modules_flags)	\
	$(NULL)

libmouse_la_LIBADD  = 		\
	$(SETTINGS_PLUGIN_LIBS)	\
	$(NULL)

libmouse_la_CFLAGS = 		\
	$(NULL)

plugin_in_files = 		\
	mouse.gnome-settings-plugin.desktop.in	\
	$(NULL)

%.gnome-settings-plugin: %.gnome-settings-plugin.desktop.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.desktop.in=.gnome-settings-plugin)

EXTRA_DIST = 			\
	$(plugin_in_files)	\
	$(NULL)

CLEANFILES = 			\
	$(plugin_DATA)		\
	$(NULL)

DISTCLEANFILES =		\
	$(plugin_DATA)		\
	$(NULL)

