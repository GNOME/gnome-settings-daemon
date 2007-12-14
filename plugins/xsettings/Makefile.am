NULL =

modules_flags = -export_dynamic -avoid-version -module -no-undefined

plugindir = $(libdir)/gnome-settings-daemon/plugins

INCLUDES = \
	-I$(top_srcdir)/src		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"\
	$(SETTINGS_PLUGIN_CFLAGS)			\
	$(NULL)

plugin_LTLIBRARIES = \
	libxsettings.la		\
	$(NULL)

libxsettings_la_SOURCES = 	\
	gnome-xsettings-plugin.h	\
	gnome-xsettings-plugin.c	\
	gnome-xsettings-manager.h	\
	gnome-xsettings-manager.c	\
	xsettings-common.h	\
	xsettings-common.c	\
	xsettings-manager.h	\
	xsettings-manager.c	\
	$(NULL)

libxsettings_la_LDFLAGS = 	\
	$(PLUGIN_LIBTOOL_FLAGS)	\
	$(modules_flags)	\
	$(NULL)

libxsettings_la_LIBADD  = 	\
	$(SETTINGS_PLUGIN_LIBS)	\
	$(NULL)

libxsettings_la_CFLAGS = 	\
	$(NULL)

plugin_in_files = 		\
	xsettings.gnome-settings-plugin.desktop.in	\
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

