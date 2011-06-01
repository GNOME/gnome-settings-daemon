plugin_name = color

plugin_LTLIBRARIES = \
	libcolor.la

libcolor_la_SOURCES = 			\
	gcm-profile-store.c		\
	gcm-profile-store.h		\
	gcm-dmi.c			\
	gcm-dmi.h			\
	gcm-edid.c			\
	gcm-edid.h			\
	gcm-tables.c			\
	gcm-tables.h			\
	gsd-color-manager.c		\
	gsd-color-manager.h		\
	gsd-color-plugin.c		\
	gsd-color-plugin.h

libcolor_la_CPPFLAGS = \
	-I$(top_srcdir)/gnome-settings-daemon		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	-DBINDIR=\"$(bindir)\"				\
	$(AM_CPPFLAGS)

libcolor_la_CFLAGS = \
	$(PLUGIN_CFLAGS)		\
	$(COLORD_CFLAGS)		\
	$(LIBCANBERRA_CFLAGS)		\
	$(LCMS_CFLAGS)			\
	$(GNOME_DESKTOP_CFLAGS)		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

libcolor_la_LDFLAGS = 			\
	$(GSD_PLUGIN_LDFLAGS)

libcolor_la_LIBADD  = 			\
	$(COLORD_LIBS)			\
	$(LIBCANBERRA_LIBS)		\
	$(GNOME_DESKTOP_LIBS)		\
	$(LCMS_LIBS)			\
	$(SETTINGS_PLUGIN_LIBS)

check_PROGRAMS =			\
	gcm-self-test

gcm_self_test_CPPFLAGS = \
	-DTESTDATADIR=\""$(top_srcdir)/plugins/color/test-data"\" \
	$(AM_CPPFLAGS)

gcm_self_test_CFLAGS =			\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(COLORD_CFLAGS)		\
	$(PLUGIN_CFLAGS)		\
	$(AM_CFLAGS)

gcm_self_test_SOURCES =			\
	gcm-tables.c			\
	gcm-tables.h			\
	gcm-dmi.c			\
	gcm-dmi.h			\
	gcm-edid.c			\
	gcm-edid.h			\
	gcm-self-test.c

gcm_self_test_LDADD =			\
	$(COLORD_LIBS)			\
	$(LCMS_LIBS)			\
	$(SETTINGS_PLUGIN_LIBS)

TESTS = gcm-self-test

plugin_in_files = 			\
	color.gnome-settings-plugin.in

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = 				\
	$(plugin_in_files)

CLEANFILES = 				\
	$(plugin_DATA)

DISTCLEANFILES =			\
	$(plugin_DATA)

@GSD_INTLTOOL_PLUGIN_RULE@
