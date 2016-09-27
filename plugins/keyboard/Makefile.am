NULL =

plugin_name = keyboard

libexec_PROGRAMS = gsd-keyboard
gsd_keyboard_SOURCES =		\
	main.c			\
	gsd-keyboard-manager.h	\
	gsd-keyboard-manager.c	\
	$(NULL)

gsd_keyboard_CFLAGS =			\
	$(PLUGIN_CFLAGS)		\
	$(KEYBOARD_CFLAGS)

gsd_keyboard_CPPFLAGS =						\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-I$(top_srcdir)/data					\
	-I$(top_srcdir)/plugins/common				\
	-DDATADIR=\""$(pkgdatadir)"\"				\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(AM_CPPFLAGS)

gsd_keyboard_LDADD =					\
	$(top_builddir)/plugins/common/libcommon.la	\
	$(KEYBOARD_LIBS)

desktopdir = $(sysconfdir)/xdg/autostart
desktop_in_files = org.gnome.SettingsDaemon.Keyboard.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

org.gnome.SettingsDaemon.Keyboard.desktop: $(desktop_in_files) Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST = 			\
	$(desktop_in_files)	\
	$(NULL)

CLEANFILES = 			\
	$(desktop_DATA)		\
	$(NULL)

DISTCLEANFILES =		\
	$(desktop_DATA)		\
	$(NULL)
