plugin_name = screensaver-proxy

libexec_PROGRAMS = gsd-screensaver-proxy

gsd_screensaver_proxy_SOURCES =				\
	main.c						\
	gsd-screensaver-proxy-manager.c		\
	gsd-screensaver-proxy-manager.h

gsd_screensaver_proxy_CPPFLAGS =				\
	-I$(top_srcdir)/data/					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon		\
	-I$(top_srcdir)/plugins/common				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	$(AM_CPPFLAGS)

gsd_screensaver_proxy_CFLAGS =		\
	$(PLUGIN_CFLAGS)		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

gsd_screensaver_proxy_LDADD =					\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(SETTINGS_DAEMON_LIBS)			\
	$(SETTINGS_PLUGIN_LIBS)

desktopdir = $(sysconfdir)/xdg/autostart
desktop_in_files = org.gnome.SettingsDaemon.ScreensaverProxy.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

org.gnome.SettingsDaemon.ScreensaverProxy.desktop: $(desktop_in_files) Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST = $(desktop_in_files)

CLEANFILES = $(desktop_DATA)

DISTCLEANFILES = $(desktop_DATA)
