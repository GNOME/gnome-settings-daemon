plugin_name = rfkill

libexec_PROGRAMS = gsd-rfkill

gsd_rfkill_SOURCES =		\
	gsd-rfkill-manager.h	\
	gsd-rfkill-manager.c	\
	rfkill-glib.c		\
	rfkill-glib.h		\
	rfkill.h		\
	main.c

gsd_rfkill_CFLAGS =						\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(PLUGIN_CFLAGS)					\
	$(RFKILL_CFLAGS)

gsd_rfkill_LDADD =						\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(RFKILL_LIBS)

desktopdir = $(sysconfdir)/xdg/autostart
desktop_in_files = org.gnome.SettingsDaemon.Rfkill.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

org.gnome.SettingsDaemon.Rfkill.desktop: $(desktop_in_files) Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

udevrulesdir = $(prefix)/lib/udev/rules.d
udevrules_DATA = 61-gnome-settings-daemon-rfkill.rules

EXTRA_DIST = $(udevrules_DATA) $(desktop_in_files)
CLEANFILES = $(desktop_DATA)
DISTCLEANFILES = $(desktop_DATA)
