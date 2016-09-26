plugin_name = print-notifications

desktopdir = $(sysconfdir)/xdg/autostart
desktop_in_files = org.gnome.SettingsDaemon.PrintNotifications.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

org.gnome.SettingsDaemon.PrintNotifications.desktop: $(desktop_in_files) Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

libexec_PROGRAMS = gsd-printer

gsd_printer_SOURCES = 	\
	gsd-printer.c

gsd_printer_CFLAGS = \
	$(SETTINGS_PLUGIN_CFLAGS)				\
	$(LIBNOTIFY_CFLAGS)					\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(AM_CFLAGS)

gsd_printer_LDADD  = 		\
	$(SETTINGS_PLUGIN_LIBS)	\
	$(CUPS_LIBS)		\
	$(LIBNOTIFY_LIBS)

libexec_PROGRAMS += gsd-print-notifications

gsd_print_notifications_SOURCES =		\
	gsd-print-notifications-manager.c	\
	gsd-print-notifications-manager.h	\
	main.c

gsd_print_notifications_CPPFLAGS =				\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	$(AM_CPPFLAGS)						\
	$(CUPS_CPPFLAGS)

gsd_print_notifications_CFLAGS =	\
	$(PLUGIN_CFLAGS)		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

gsd_print_notifications_LDADD =				\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(SETTINGS_DAEMON_LIBS)					\
	$(SETTINGS_PLUGIN_LIBS)					\
	$(CUPS_LIBS)						\
	$(LIBNOTIFY_LIBS)

EXTRA_DIST = 			\
	$(desktop_in_files)

CLEANFILES = 			\
	$(desktop_DATA)

DISTCLEANFILES =		\
	$(desktop_DATA)
