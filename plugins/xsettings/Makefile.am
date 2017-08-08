NULL =

plugin_name = xsettings

noinst_PROGRAMS = test-gtk-modules

test_gtk_modules_SOURCES =	\
	gsd-xsettings-gtk.c	\
	gsd-xsettings-gtk.h	\
	test-gtk-modules.c

test_gtk_modules_CFLAGS = $(PLUGIN_CFLAGS) $(XSETTINGS_CFLAGS)

test_gtk_modules_LDADD =					\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(XSETTINGS_LIBS)

test_gtk_modules_CPPFLAGS =				\
	-I$(top_srcdir)/gnome-settings-daemon		\
	-I$(top_builddir)/gnome-settings-daemon		\
	-I$(top_srcdir)/data/				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	-DGTK_MODULES_DIRECTORY=\""$(libdir)/gnome-settings-daemon-@GSD_API_VERSION@/gtk-modules/"\"

noinst_PROGRAMS += test-fontconfig-monitor

test_fontconfig_monitor_SOURCES =	\
	fc-monitor.c			\
	fc-monitor.h

test_fontconfig_monitor_CFLAGS = $(PLUGIN_CFLAGS) $(XSETTINGS_CFLAGS)

test_fontconfig_monitor_LDADD = $(XSETTINGS_LIBS)

test_fontconfig_monitor_CPPFLAGS =			\
	-I$(top_srcdir)/gnome-settings-daemon		\
	-I$(top_builddir)/gnome-settings-daemon		\
	-I$(top_srcdir)/data/				\
	-DFONTCONFIG_MONITOR_TEST

noinst_PROGRAMS += test-wm-button-layout-translations

test_wm_button_layout_translations_SOURCES =	\
	test-wm-button-layout-translations.c	\
	wm-button-layout-translation.c		\
	wm-button-layout-translation.h

test_wm_button_layout_translations_CFLAGS = $(XSETTINGS_CFLAGS)

test_wm_button_layout_translations_LDADD = $(XSETTINGS_LIBS)

libexec_PROGRAMS = gsd-xsettings

gsd_xsettings_SOURCES =		\
	gsd-xsettings-gtk.c	\
	gsd-xsettings-gtk.h	\
	gsd-xsettings-manager.c	\
	gsd-xsettings-manager.h	\
	gsd-xsettings-gtk.h	\
	xsettings-common.c	\
	xsettings-common.h	\
	xsettings-manager.c	\
	xsettings-manager.h	\
	fc-monitor.c	\
	fc-monitor.h	\
	gsd-remote-display-manager.c \
	gsd-remote-display-manager.h \
	wm-button-layout-translation.c	\
	wm-button-layout-translation.h	\
	main.c

gsd_xsettings_CFLAGS =			\
	$(PLUGIN_CFLAGS)		\
	$(XSETTINGS_CFLAGS)

gsd_xsettings_CPPFLAGS =										\
	-I$(top_srcdir)/gnome-settings-daemon								\
	-I$(top_builddir)/gnome-settings-daemon								\
	-I$(top_srcdir)/plugins/common/									\
	-I$(top_srcdir)/data/										\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"						\
	-DGTK_MODULES_DIRECTORY=\""$(libdir)/gnome-settings-daemon-@GSD_API_VERSION@/gtk-modules/"\"

gsd_xsettings_LDADD =						\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(XSETTINGS_LIBS)

desktopdir = $(sysconfdir)/xdg/autostart
desktop_in_files = org.gnome.SettingsDaemon.XSettings.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

org.gnome.SettingsDaemon.XSettings.desktop: $(desktop_in_files) Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST = 			\
	README.xsettings	\
	$(desktop_in_files)	\
	$(NULL)

CLEANFILES = 			\
	$(desktop_DATA)		\
	$(NULL)

DISTCLEANFILES =		\
	$(desktop_DATA)		\
	$(NULL)
