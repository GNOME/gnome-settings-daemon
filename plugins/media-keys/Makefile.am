icondir = $(datadir)/icons/hicolor
context = actions

plugin_name = media-keys

NULL =

SUBDIRS =
plugin_LTLIBRARIES =

if HAVE_PULSE
SUBDIRS += cut-n-paste
plugin_LTLIBRARIES += libmedia-keys.la
endif

BUILT_SOURCES = 			\
	gsd-marshal.h			\
	gsd-marshal.c			\
	$(NULL)

ICON_FILES = 			\
	touchpad-disabled-16.png	\
	touchpad-enabled-16.png		\
	touchpad-disabled-22.png	\
	touchpad-enabled-22.png		\
	touchpad-disabled-24.png	\
	touchpad-enabled-24.png		\
	touchpad-disabled-32.png	\
	touchpad-enabled-32.png		\
	touchpad-disabled-48.png	\
	touchpad-enabled-48.png		\
	touchpad-disabled.svg		\
	touchpad-enabled.svg		\
	keyboard-brightness-symbolic.svg

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(icondir)/16x16/$(context)
	$(mkinstalldirs) $(DESTDIR)$(icondir)/22x22/$(context)
	$(mkinstalldirs) $(DESTDIR)$(icondir)/24x24/$(context)
	$(mkinstalldirs) $(DESTDIR)$(icondir)/32x32/$(context)
	$(mkinstalldirs) $(DESTDIR)$(icondir)/scalable/$(context)
	$(INSTALL_DATA) $(srcdir)/touchpad-enabled-16.png $(DESTDIR)$(icondir)/16x16/$(context)/touchpad-enabled.png
	$(INSTALL_DATA) $(srcdir)/touchpad-enabled-22.png $(DESTDIR)$(icondir)/22x22/$(context)/touchpad-enabled.png
	$(INSTALL_DATA) $(srcdir)/touchpad-enabled-24.png $(DESTDIR)$(icondir)/24x24/$(context)/touchpad-enabled.png
	$(INSTALL_DATA) $(srcdir)/touchpad-enabled-32.png $(DESTDIR)$(icondir)/32x32/$(context)/touchpad-enabled.png
	$(INSTALL_DATA) $(srcdir)/touchpad-enabled.svg $(DESTDIR)$(icondir)/scalable/$(context)/touchpad-enabled.svg
	$(INSTALL_DATA) $(srcdir)/touchpad-disabled-16.png $(DESTDIR)$(icondir)/16x16/$(context)/touchpad-disabled.png
	$(INSTALL_DATA) $(srcdir)/touchpad-disabled-22.png $(DESTDIR)$(icondir)/22x22/$(context)/touchpad-disabled.png
	$(INSTALL_DATA) $(srcdir)/touchpad-disabled-24.png $(DESTDIR)$(icondir)/24x24/$(context)/touchpad-disabled.png
	$(INSTALL_DATA) $(srcdir)/touchpad-disabled-32.png $(DESTDIR)$(icondir)/32x32/$(context)/touchpad-disabled.png
	$(INSTALL_DATA) $(srcdir)/touchpad-disabled.svg $(DESTDIR)$(icondir)/scalable/$(context)/touchpad-disabled.svg
	$(INSTALL_DATA) $(srcdir)/keyboard-brightness-symbolic.svg $(DESTDIR)$(icondir)/scalable/$(context)/keyboard-brightness-symbolic.svg

uninstall-local:
	rm -f $(DESTDIR)$(icondir)/16x16/$(context)/touchpad-enabled.png
	rm -f $(DESTDIR)$(icondir)/22x22/$(context)/touchpad-enabled.png
	rm -f $(DESTDIR)$(icondir)/24x24/$(context)/touchpad-enabled.png
	rm -f $(DESTDIR)$(icondir)/32x32/$(context)/touchpad-enabled.png
	rm -f $(DESTDIR)$(icondir)/scalable/$(context)/touchpad-enabled.svg
	rm -f $(DESTDIR)$(icondir)/16x16/$(context)/touchpad-disabled.png
	rm -f $(DESTDIR)$(icondir)/22x22/$(context)/touchpad-disabled.png
	rm -f $(DESTDIR)$(icondir)/24x24/$(context)/touchpad-disabled.png
	rm -f $(DESTDIR)$(icondir)/32x32/$(context)/touchpad-disabled.png
	rm -f $(DESTDIR)$(icondir)/scalable/$(context)/touchpad-disabled.svg

gsd-marshal.c: gsd-marshal.list
	$(AM_V_GEN) $(GLIB_GENMARSHAL) --prefix=gsd_marshal $< --header --body --internal > $@

gsd-marshal.h: gsd-marshal.list
	$(AM_V_GEN) $(GLIB_GENMARSHAL) --prefix=gsd_marshal $< --header --internal > $@

libmedia_keys_la_SOURCES = 		\
	gsd-media-keys-plugin.h		\
	gsd-media-keys-plugin.c		\
	gsd-media-keys-manager.h	\
	gsd-media-keys-manager.c	\
	gsd-media-keys-window.h		\
	gsd-media-keys-window.c		\
	gsd-osd-window.c		\
	gsd-osd-window.h		\
	acme.h				\
	$(BUILT_SOURCES)		\
	$(NULL)

libmedia_keys_la_CPPFLAGS = \
	-I$(top_srcdir)/data/					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-I$(top_srcdir)/plugins/media-keys/cut-n-paste		\
	-DBINDIR=\"$(bindir)\"					\
	-DPIXMAPDIR=\""$(pkgdatadir)"\"				\
	-DGTKBUILDERDIR=\""$(pkgdatadir)"\"			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(AM_CPPFLAGS)

libmedia_keys_la_CFLAGS = \
	$(PLUGIN_CFLAGS)		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(MEDIA_KEYS_CFLAGS)		\
	$(AM_CFLAGS)

libmedia_keys_la_LDFLAGS = 		\
	$(GSD_PLUGIN_LDFLAGS)

libmedia_keys_la_LIBADD  = 		\
	$(top_builddir)/plugins/common/libcommon.la			\
	$(top_builddir)/plugins/media-keys/cut-n-paste/libgvc.la	\
	$(MEDIA_KEYS_LIBS)						\
	$(SETTINGS_PLUGIN_LIBS)						\
	-lm

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
	gsd-osd-window.c			\
	gsd-osd-window.h			\
	gsd-media-keys-window.c			\
	gsd-media-keys-window.h			\
	test-media-window.c			\
	$(NULL)

test_media_window_CPPFLAGS = \
	-I$(top_srcdir)/data/					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-I$(top_srcdir)/plugins/media-keys/cut-n-paste		\
	-DDATADIR=\""$(datadir)"\"				\
	-DPIXMAPDIR=\""$(pkgdatadir)"\"				\
	-DGTKBUILDERDIR=\""$(pkgdatadir)"\"			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(AM_CPPFLAGS)

test_media_window_CFLAGS = \
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

test_media_window_LDADD = \
	$(top_builddir)/plugins/common/libcommon.la			\
	$(SETTINGS_DAEMON_LIBS)			\
	$(SETTINGS_PLUGIN_LIBS)			\
	-lm

test_media_keys_SOURCES =			\
	gsd-media-keys-manager.c		\
	gsd-media-keys-manager.h		\
	gsd-media-keys-window.h			\
	gsd-media-keys-window.c			\
	gsd-osd-window.c			\
	gsd-osd-window.h			\
	test-media-keys.c			\
	$(BUILT_SOURCES)			\
	$(NULL)

test_media_keys_CPPFLAGS = \
	-I$(top_srcdir)/data/					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-I$(top_srcdir)/plugins/media-keys/cut-n-paste		\
	-DBINDIR=\"$(bindir)\"					\
	-DPIXMAPDIR=\""$(pkgdatadir)"\"				\
	-DGTKBUILDERDIR=\""$(pkgdatadir)"\"			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(AM_CPPFLAGS)

test_media_keys_CFLAGS = \
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(MEDIA_KEYS_CFLAGS)		\
	$(AM_CFLAGS)

test_media_keys_LDADD = \
	$(top_builddir)/gnome-settings-daemon/libgsd-profile.la		\
	$(top_builddir)/plugins/common/libcommon.la			\
	$(SETTINGS_DAEMON_LIBS)			\
	$(SETTINGS_PLUGIN_LIBS)			\
	$(MEDIA_KEYS_LIBS)			\
	-lm

if HAVE_PULSE
test_media_keys_LDADD += $(top_builddir)/plugins/media-keys/cut-n-paste/libgvc.la
endif

DIST_SUBDIRS = cut-n-paste

EXTRA_DIST = 				\
	gsd-marshal.list		\
	$(plugin_in_files)		\
	$(pixmaps_DATA)			\
	touchpad-enabled-template.svg	\
	touchpad-disabled-template.svg	\
	$(ICON_FILES)

CLEANFILES = 			\
	$(BUILT_SOURCES)	\
	$(plugin_DATA)

DISTCLEANFILES =		\
	$(plugin_DATA)

@GSD_INTLTOOL_PLUGIN_RULE@
