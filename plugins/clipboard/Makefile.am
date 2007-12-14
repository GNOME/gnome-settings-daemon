NULL =

modules_flags = -export_dynamic -avoid-version -module -no-undefined

plugindir = $(libdir)/gnome-settings-daemon/plugins

INCLUDES = \
	-I$(top_srcdir)/src		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"\
	$(SETTINGS_PLUGIN_CFLAGS)			\
	$(NULL)

plugin_LTLIBRARIES = \
	libclipboard.la		\
	$(NULL)

libclipboard_la_SOURCES = 	\
	gsd-clipboard-plugin.h	\
	gsd-clipboard-plugin.c	\
	gsd-clipboard-manager.h	\
	gsd-clipboard-manager.c	\
	xutils.h		\
	xutils.c		\
	list.h			\
	list.c			\
	$(NULL)

libclipboard_la_LDFLAGS = 	\
	$(PLUGIN_LIBTOOL_FLAGS)	\
	$(modules_flags)	\
	$(NULL)

libclipboard_la_LIBADD  = 	\
	$(SETTINGS_PLUGIN_LIBS)	\
	$(NULL)

libclipboard_la_CFLAGS = 	\
	$(NULL)

plugin_in_files = 		\
	clipboard.gnome-settings-plugin.desktop.in	\
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

