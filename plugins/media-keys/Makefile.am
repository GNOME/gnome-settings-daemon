NULL =

SUBDIRS =		\
	actions		\
	$(NULL)

modules_flags = -export_dynamic -avoid-version -module -no-undefined

plugindir = $(libdir)/gnome-settings-daemon/plugins/media-keys

INCLUDES = \
	-I$(top_srcdir)/src		\
	-I$(top_builddir)/plugins/media-keys/actions	\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"\
	-DPIXMAPDIR=\""$(plugindir)"\"			\
	-DGLADEDIR=\""$(plugindir)"\"			\
	$(SETTINGS_PLUGIN_CFLAGS)			\
	$(NULL)

plugin_LTLIBRARIES = \
	libmedia-keys.la		\
	$(NULL)

BUILT_SOURCES = 			\
	gsd-media-keys-manager-glue.h	\
	gsd-marshal.h			\
	gsd-marshal.c			\
	$(NULL)

gsd-media-keys-manager-glue.h: gsd-media-keys-manager.xml Makefile
	dbus-binding-tool --prefix=gsd_media_keys_manager --mode=glib-server $< > xgen-$(@F) \
	&& ( cmp -s xgen-$(@F) $@ || cp xgen-$(@F) $@ ) \
	&& rm -f xgen-$(@F)

gsd-marshal.c: gsd-marshal.list
	$(GLIB_GENMARSHAL) --prefix=gsd_marshal $< --header --body --internal > $@

gsd-marshal.h: gsd-marshal.list
	$(GLIB_GENMARSHAL) --prefix=gsd_marshal $< --header --internal > $@

libmedia_keys_la_SOURCES = 		\
	gsd-media-keys-plugin.h		\
	gsd-media-keys-plugin.c		\
	gsd-media-keys-manager.h	\
	gsd-media-keys-manager.c	\
	gsd-media-keys-window.h		\
	gsd-media-keys-window.c		\
	eggaccelerators.h		\
	eggaccelerators.c		\
	$(BUILT_SOURCES)		\
	$(NULL)

libmedia_keys_la_LDFLAGS = 		\
	$(PLUGIN_LIBTOOL_FLAGS)		\
	$(modules_flags)		\
	$(NULL)

libmedia_keys_la_LIBADD  = 		\
	$(SETTINGS_PLUGIN_LIBS)		\
	$(XF86MISC_LIBS)		\
	$(GST_LIBS)			\
	$(top_builddir)/plugins/media-keys/actions/libacme.la	\
	$(NULL)

libmedia_keys_la_CFLAGS = 		\
	$(NULL)

plugin_in_files = 		\
	media-keys.gnome-settings-plugin.in	\
	$(NULL)

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

noinst_PROGRAMS =				\
	test-media-window			\
	$(NULL)

test_media_window_SOURCES =			\
	gsd-media-keys-window.c			\
	gsd-media-keys-window.h			\
	test-media-window.c			\
	$(NULL)

test_media_window_LDADD = \
	$(SETTINGS_DAEMON_LIBS)			\
	$(SETTINGS_PLUGIN_LIBS)			\
	$(XF86MISC_LIBS)			\
	$(GST_LIBS)				\
	$(NULL)

EXTRA_DIST = 				\
	gsd-media-keys-manager.xml	\
	gsd-marshal.list		\
	$(plugin_in_files)		\
	$(NULL)

CLEANFILES = 			\
	$(BUILT_SOURCES)	\
	$(plugin_DATA)		\
	$(NULL)

DISTCLEANFILES =		\
	$(plugin_DATA)		\
	$(NULL)

@GSD_INTLTOOL_PLUGIN_RULE@
