plugin_name = a11y-settings

libexec_PROGRAMS = gsd-a11y-settings

gsd_a11y_settings_SOURCES =	\
	gsd-a11y-settings-manager.h	\
	gsd-a11y-settings-manager.c	\
	main.c

gsd_a11y_settings_CFLAGS =					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(PLUGIN_CFLAGS)					\
	$(A11Y_SETTINGS_CFLAGS)

gsd_a11y_settings_LDADD =					\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(A11Y_SETTINGS_LIBS)

desktopdir = $(sysconfdir)/xdg/autostart
desktop_in_files = org.gnome.SettingsDaemon.A11ySettings.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

org.gnome.SettingsDaemon.A11ySettings.desktop: $(desktop_in_files) Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST = 			\
	$(desktop_in_files)

CLEANFILES = 			\
	$(desktop_DATA)

DISTCLEANFILES =		\
	$(desktop_DATA)
