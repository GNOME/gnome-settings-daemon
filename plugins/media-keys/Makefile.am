icondir = $(datadir)/icons/hicolor
context = actions

plugin_name = media-keys

NULL =

SUBDIRS = gvc

BUILT_SOURCES = 			\
	gsd-marshal.h			\
	gsd-marshal.c			\
	shell-key-grabber.c		\
	shell-key-grabber.h		\
	$(NULL)

gsd-marshal.c: gsd-marshal.list
	$(AM_V_GEN) $(GLIB_GENMARSHAL) --prefix=gsd_marshal $< --header --body --internal > $@

gsd-marshal.h: gsd-marshal.list
	$(AM_V_GEN) $(GLIB_GENMARSHAL) --prefix=gsd_marshal $< --header --internal > $@

shell-key-grabber.c: shell-key-grabber.h
shell-key-grabber.h: Makefile.am org.gnome.ShellKeyGrabber.xml
	gdbus-codegen --interface-prefix org.gnome.		\
	              --generate-c-code shell-key-grabber	\
	              --c-namespace Shell			\
	              $(srcdir)/org.gnome.ShellKeyGrabber.xml

desktopdir = $(sysconfdir)/xdg/autostart
desktop_in_files = org.gnome.SettingsDaemon.MediaKeys.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

org.gnome.SettingsDaemon.MediaKeys.desktop: $(desktop_in_files) Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

libexec_PROGRAMS = gsd-media-keys

gsd_media_keys_SOURCES =			\
	gsd-media-keys-manager.c		\
	gsd-media-keys-manager.h		\
	gsd-screenshot-utils.h			\
	gsd-screenshot-utils.c			\
	bus-watch-namespace.c			\
	bus-watch-namespace.h			\
	mpris-controller.c			\
	mpris-controller.h			\
	media-keys.h				\
	shell-action-modes.h			\
	shortcuts-list.h			\
	main.c					\
	$(BUILT_SOURCES)			\
	$(NULL)

gsd_media_keys_CPPFLAGS = \
	-I$(top_srcdir)/data/					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-I$(top_srcdir)/plugins/media-keys/gvc			\
	-DBINDIR=\"$(bindir)\"					\
	-DPIXMAPDIR=\""$(pkgdatadir)"\"				\
	-DGTKBUILDERDIR=\""$(pkgdatadir)"\"			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(AM_CPPFLAGS)

gsd_media_keys_CFLAGS =			\
	$(PLUGIN_CFLAGS)		\
	$(MEDIA_KEYS_CFLAGS)

gsd_media_keys_LDADD = \
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(top_builddir)/plugins/media-keys/gvc/libgvc.la	\
	$(MEDIA_KEYS_LIBS)					\
	$(LIBM)

EXTRA_DIST = 				\
	gsd-marshal.list		\
	README.media-keys-API		\
	org.gnome.ShellKeyGrabber.xml	\
	$(plugin_in_files)		\
	$(desktop_in_files)

CLEANFILES = 			\
	$(BUILT_SOURCES)	\
	$(desktop_DATA)

DISTCLEANFILES =		\
	$(desktop_DATA)
