plugin_name = datetime

timedated.c: timedated.h
timedated.h: Makefile.am timedated1-interface.xml
	gdbus-codegen						\
		--interface-prefix org.freedesktop.		\
		--generate-c-code timedated			\
		$(srcdir)/timedated1-interface.xml

BUILT_SOURCES =			\
	timedated.c		\
	timedated.h

tzdatadir = $(pkgdatadir)/datetime
dist_tzdata_DATA = backward

libexec_PROGRAMS = gsd-datetime

gsd_datetime_SOURCES =	\
	$(BUILT_SOURCES)	\
	gsd-datetime-manager.c	\
	gsd-datetime-manager.h	\
	gsd-timezone-monitor.c	\
	gsd-timezone-monitor.h	\
	main.c			\
	tz.c			\
	tz.h			\
	weather-tz.c		\
	weather-tz.h

gsd_datetime_CFLAGS =					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	-DGNOMECC_DATA_DIR="\"$(pkgdatadir)\""			\
	$(DATETIME_CFLAGS)					\
	$(PLUGIN_CFLAGS)

gsd_datetime_LDADD =					\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(DATETIME_LIBS)					\
	$(LIBM)

desktopdir = $(sysconfdir)/xdg/autostart
desktop_in_files = org.gnome.SettingsDaemon.Datetime.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

org.gnome.SettingsDaemon.Datetime.desktop: $(desktop_in_files) Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST =				\
	timedated1-interface.xml	\
	$(desktop_in_files)

CLEANFILES = \
	$(desktop_DATA)

DISTCLEANFILES = \
	$(desktop_DATA)
