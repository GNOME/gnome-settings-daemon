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
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(PRINT_NOTIFICATIONS_CFLAGS)				\
	$(AM_CFLAGS)

gsd_printer_LDADD  =			\
	$(PRINT_NOTIFICATIONS_LIBS)	\
	$(CUPS_LIBS)

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

gsd_print_notifications_CFLAGS =			\
	$(PLUGIN_CFLAGS)				\
	$(PRINT_NOTIFICATIONS_CFLAGS)

gsd_print_notifications_LDADD =				\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(CUPS_LIBS)						\
	$(PRINT_NOTIFICATIONS_LIBS)

EXTRA_DIST = 			\
	$(desktop_in_files)

CLEANFILES = 			\
	$(desktop_DATA)

DISTCLEANFILES =		\
	$(desktop_DATA)
