plugin_name = housekeeping

COMMON_FILES =				\
	gsd-disk-space.c		\
	gsd-disk-space.h		\
	gsd-disk-space-helper.h		\
	gsd-disk-space-helper.c

noinst_PROGRAMS = gsd-disk-space-test gsd-empty-trash-test gsd-purge-temp-test

gsd_disk_space_test_SOURCES =		\
	gsd-disk-space-test.c		\
	$(COMMON_FILES)
gsd_disk_space_test_LDADD = $(SETTINGS_PLUGIN_LIBS) $(GIOUNIX_LIBS) $(LIBNOTIFY_LIBS)
gsd_disk_space_test_CFLAGS =		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(GIOUNIX_CFLAGS)		\
	$(LIBNOTIFY_CFLAGS)		\
	$(AM_CFLAGS)

gsd_empty_trash_test_SOURCES =		\
	gsd-empty-trash-test.c		\
	$(COMMON_FILES)
gsd_empty_trash_test_LDADD = $(SETTINGS_PLUGIN_LIBS) $(GIOUNIX_LIBS) $(LIBNOTIFY_LIBS)
gsd_empty_trash_test_CFLAGS =		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(GIOUNIX_CFLAGS)		\
	$(LIBNOTIFY_CFLAGS)		\
	$(AM_CFLAGS)

gsd_purge_temp_test_SOURCES =		\
	gsd-purge-temp-test.c		\
	$(COMMON_FILES)
gsd_purge_temp_test_LDADD = $(SETTINGS_PLUGIN_LIBS) $(GIOUNIX_LIBS) $(LIBNOTIFY_LIBS)
gsd_purge_temp_test_CFLAGS =		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(GIOUNIX_CFLAGS)		\
	$(LIBNOTIFY_CFLAGS)		\
	$(AM_CFLAGS)

libexec_PROGRAMS = gsd-test-housekeeping

gsd_test_housekeeping_SOURCES =		\
	test-housekeeping.c		\
	gsd-housekeeping-manager.c	\
	gsd-housekeeping-manager.h	\
	$(COMMON_FILES)

gsd_test_housekeeping_CPPFLAGS =				\
	-I$(top_srcdir)/data/					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	$(AM_CPPFLAGS)

gsd_test_housekeeping_CFLAGS =		\
	$(PLUGIN_CFLAGS)		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(MOUSE_CFLAGS)			\
	$(AM_CFLAGS)

gsd_test_housekeeping_LDADD =					\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(SETTINGS_DAEMON_LIBS)					\
	$(SETTINGS_PLUGIN_LIBS)

plugin_LTLIBRARIES = libhousekeeping.la

libhousekeeping_la_SOURCES =		\
	$(COMMON_FILES)			\
	gsd-housekeeping-manager.c	\
	gsd-housekeeping-manager.h	\
	gsd-housekeeping-plugin.c

libhousekeeping_la_CPPFLAGS = 					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(AM_CPPFLAGS)

libhousekeeping_la_CFLAGS = 		\
	$(PLUGIN_CFLAGS)		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(GIOUNIX_CFLAGS)		\
	$(LIBNOTIFY_CFLAGS)		\
	$(AM_CFLAGS)

libhousekeeping_la_LDFLAGS = $(GSD_PLUGIN_LDFLAGS)

libhousekeeping_la_LIBADD = $(SETTINGS_PLUGIN_LIBS) $(GIOUNIX_LIBS) $(LIBNOTIFY_LIBS)

plugin_in_files = housekeeping.gnome-settings-plugin.in

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = $(plugin_in_files)

CLEANFILES = $(plugin_DATA)

DISTCLEANFILES = $(plugin_DATA)

@GSD_INTLTOOL_PLUGIN_RULE@
