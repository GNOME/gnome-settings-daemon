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

libexec_PROGRAMS = gsd-test-datetime

gsd_test_datetime_SOURCES =	\
	$(BUILT_SOURCES)	\
	gsd-datetime-manager.c	\
	gsd-datetime-manager.h	\
	gsd-timezone-monitor.c	\
	gsd-timezone-monitor.h	\
	test-datetime.c		\
	tz.c			\
	tz.h			\
	weather-tz.c		\
	weather-tz.h

gsd_test_datetime_CFLAGS =					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	-DGNOMECC_DATA_DIR="\"$(pkgdatadir)\""			\
	$(DATETIME_CFLAGS)					\
	$(PLUGIN_CFLAGS)					\
	$(SETTINGS_PLUGIN_CFLAGS)				\
	$(AM_CFLAGS)

gsd_test_datetime_LDADD =					\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(DATETIME_LIBS)					\
	$(LIBM)							\
	$(SETTINGS_PLUGIN_LIBS)

plugin_LTLIBRARIES = \
	libdatetime.la

libdatetime_la_SOURCES =	\
	$(BUILT_SOURCES)	\
	gsd-datetime-plugin.c	\
	gsd-datetime-manager.h	\
	gsd-datetime-manager.c	\
	gsd-timezone-monitor.h	\
	gsd-timezone-monitor.c	\
	tz.h			\
	tz.c			\
	weather-tz.c		\
	weather-tz.h

libdatetime_la_CPPFLAGS =					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	-DGNOMECC_DATA_DIR="\"$(pkgdatadir)\""			\
	$(AM_CPPFLAGS)

libdatetime_la_CFLAGS =			\
	$(DATETIME_CFLAGS)		\
	$(PLUGIN_CFLAGS)		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

libdatetime_la_LDFLAGS = \
	$(GSD_PLUGIN_LDFLAGS)

libdatetime_la_LIBADD = \
	$(DATETIME_LIBS)						\
	$(LIBM) \
	$(SETTINGS_PLUGIN_LIBS)

plugin_in_files = \
	datetime.gnome-settings-plugin.in

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST =				\
	timedated1-interface.xml	\
	$(plugin_in_files)

CLEANFILES = \
	$(plugin_DATA)

DISTCLEANFILES = \
	$(plugin_DATA)

@GSD_INTLTOOL_PLUGIN_RULE@
