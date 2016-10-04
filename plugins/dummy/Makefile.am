plugin_name = dummy

libexec_PROGRAMS = gsd-dummy
gsd_dummy_SOURCES =			\
	main.c				\
	gsd-dummy-manager.c		\
	gsd-dummy-manager.h

gsd_dummy_CFLAGS =			\
	$(PLUGIN_CFLAGS)		\
	$(DUMMY_CFLAGS)

gsd_dummy_CPPFLAGS =			\
	-I$(top_srcdir)/gnome-settings-daemon		\
	-I$(top_builddir)/gnome-settings-daemon		\
	-I$(top_srcdir)/plugins/common			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	$(AM_CPPFLAGS)

gsd_dummy_LDADD = $(DUMMY_LIBS) $(top_builddir)/gnome-settings-daemon/libgsd.la

desktopdir = $(sysconfdir)/xdg/autostart

desktop_DATA =
if !SMARTCARD_SUPPORT
desktop_DATA += org.gnome.SettingsDaemon.Smartcard.desktop
endif
if !HAVE_WACOM
desktop_DATA += org.gnome.SettingsDaemon.Wacom.desktop
endif
if !BUILD_PRINT_NOTIFICATIONS
desktop_DATA += org.gnome.SettingsDaemon.PrintNotifications.desktop
endif
if !BUILD_RFKILL
desktop_DATA += org.gnome.SettingsDaemon.Rfkill.desktop
endif

%.desktop: org.gnome.SettingsDaemon.Dummy.desktop.in Makefile
	$(AM_V_GEN) PLUGINNAME=`echo $@ | sed 's,.desktop,,'` && sed -e "s|\@pluginname\@|$$PLUGINNAME|" -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST = org.gnome.SettingsDaemon.Dummy.desktop.in

CLEANFILES = $(NULL)

DISTCLEANFILES = $(NULL)
