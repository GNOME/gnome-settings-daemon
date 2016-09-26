plugin_name = orientation

libexec_PROGRAMS = gsd-orientation

gsd_orientation_SOURCES =		\
	gsd-orientation-manager.h	\
	gsd-orientation-manager.c	\
	main.c

gsd_orientation_CFLAGS =					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(ORIENTATION_CFLAGS)					\
	$(PLUGIN_CFLAGS)					\
	$(SETTINGS_PLUGIN_CFLAGS)				\
	$(AM_CFLAGS)

gsd_orientation_LDADD =						\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(ORIENTATION_LIBS)					\
	$(SETTINGS_PLUGIN_LIBS)

desktopdir = $(sysconfdir)/xdg/autostart
desktop_in_files = org.gnome.SettingsDaemon.Orientation.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

org.gnome.SettingsDaemon.Orientation.desktop: $(desktop_in_files) Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST = $(desktop_in_files)
CLEANFILES = $(desktop_DATA)
DISTCLEANFILES = $(desktop_DATA)
