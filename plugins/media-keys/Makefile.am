NULL =

SUBDIRS =
plugin_LTLIBRARIES =

if HAVE_PULSE
SUBDIRS += cut-n-paste
plugin_LTLIBRARIES += libmedia-keys.la
endif

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
	acme.h				\
	$(BUILT_SOURCES)		\
	$(NULL)

libmedia_keys_la_CPPFLAGS = \
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-I$(top_srcdir)/plugins/media-keys/cut-n-paste		\
	-DPIXMAPDIR=\""$(pkgdatadir)"\"				\
	-DGTKBUILDERDIR=\""$(pkgdatadir)"\"			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(AM_CPPFLAGS)

libmedia_keys_la_CFLAGS = \
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

libmedia_keys_la_LDFLAGS = 		\
	$(GSD_PLUGIN_LDFLAGS)

libmedia_keys_la_LIBADD  = 		\
	$(top_builddir)/plugins/common/libcommon.la			\
	$(top_builddir)/plugins/media-keys/cut-n-paste/libgvc.la	\
	$(SETTINGS_PLUGIN_LIBS)						\
	$(XF86MISC_LIBS)

plugin_in_files = 		\
	media-keys.gnome-settings-plugin.in

if HAVE_PULSE
plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)
endif

noinst_PROGRAMS =				\
	test-media-keys				\
	test-media-window			\
	$(NULL)

test_media_window_SOURCES =			\
	gsd-media-keys-window.c			\
	gsd-media-keys-window.h			\
	test-media-window.c			\
	$(NULL)

test_media_window_CPPFLAGS = \
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/media-keys/cut-n-paste		\
	-DPIXMAPDIR=\""$(pkgdatadir)"\"				\
	-DGTKBUILDERDIR=\""$(pkgdatadir)"\"			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(AM_CPPFLAGS)

test_media_window_CFLAGS = \
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

test_media_window_LDADD = \
	$(SETTINGS_DAEMON_LIBS)			\
	$(SETTINGS_PLUGIN_LIBS)			\
	$(XF86MISC_LIBS)			\
	$(GST_LIBS)

test_media_keys_SOURCES =			\
	gsd-media-keys-manager.c		\
	gsd-media-keys-manager.h		\
	gsd-media-keys-window.h			\
	gsd-media-keys-window.c			\
	test-media-keys.c			\
	$(BUILT_SOURCES)			\
	$(NULL)

test_media_keys_CPPFLAGS = \
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-I$(top_srcdir)/plugins/media-keys/cut-n-paste		\
	-DPIXMAPDIR=\""$(pkgdatadir)"\"				\
	-DGTKBUILDERDIR=\""$(pkgdatadir)"\"			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(AM_CPPFLAGS)

test_media_keys_CFLAGS = \
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

test_media_keys_LDADD = \
	$(top_builddir)/gnome-settings-daemon/libgsd-profile.la		\
	$(top_builddir)/plugins/common/libcommon.la			\
	$(SETTINGS_DAEMON_LIBS)			\
	$(SETTINGS_PLUGIN_LIBS)			\
	$(XF86MISC_LIBS)			\
	$(GST_LIBS)

if HAVE_PULSE
test_media_keys_LDADD += $(top_builddir)/plugins/media-keys/cut-n-paste/libgvc.la
endif

gtkbuilderdir = $(pkgdatadir)
gtkbuilder_DATA =	\
	acme.ui		\
	$(NULL)

DIST_SUBDIRS = cut-n-paste

EXTRA_DIST = 				\
	gsd-media-keys-manager.xml	\
	gsd-marshal.list		\
	$(plugin_in_files)		\
	$(gtkbuilder_DATA)		\
	$(pixmaps_DATA)

CLEANFILES = 			\
	$(BUILT_SOURCES)	\
	$(plugin_DATA)

DISTCLEANFILES =		\
	$(plugin_DATA)

@GSD_INTLTOOL_PLUGIN_RULE@
