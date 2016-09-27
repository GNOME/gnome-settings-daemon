NULL =

plugin_name = clipboard

libexec_PROGRAMS = gsd-clipboard
gsd_clipboard_SOURCES =	\
	main.c			\
	gsd-clipboard-manager.h	\
	gsd-clipboard-manager.c	\
	xutils.h		\
	xutils.c		\
	list.h			\
	list.c			\
	$(NULL)

gsd_clipboard_CFLAGS =		\
	$(CLIPBOARD_CFLAGS)	\
	$(AM_CFLAGS)

gsd_clipboard_CPPFLAGS =					\
	$(PLUGIN_CFLAGS)					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"

gsd_clipboard_LDADD = $(CLIPBOARD_LIBS) $(top_builddir)/gnome-settings-daemon/libgsd.la

desktopdir = $(sysconfdir)/xdg/autostart
desktop_in_files = org.gnome.SettingsDaemon.Clipboard.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

org.gnome.SettingsDaemon.Clipboard.desktop: $(desktop_in_files) Makefile
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
