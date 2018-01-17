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
	-DLIBEXECDIR=\""$(libexecdir)"\"

gsd_screensaver_proxy_CFLAGS =		\
	$(PLUGIN_CFLAGS)		\
	$(SCREENSAVER_PROXY_CFLAGS)	\
	$(AM_CFLAGS)

gsd_screensaver_proxy_LDADD =					\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(SCREENSAVER_PROXY_LIBS)

unitdir = $(systemduserunitdir)
unit_in_files = gsd-screensaver-proxy.service.in
unit_DATA = gsd-screensaver-proxy.service

gsd-screensaver-proxy.service: $(unit_in_files) Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST = $(unit_in_files)

CLEANFILES = $(unit_DATA)

DISTCLEANFILES = $(unit_DATA)
