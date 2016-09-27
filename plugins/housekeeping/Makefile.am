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
gsd_disk_space_test_LDADD = $(HOUSEKEEPING_LIBS)
gsd_disk_space_test_CFLAGS =		\
	$(HOUSEKEEPING_CFLAGS)

gsd_empty_trash_test_SOURCES =		\
	gsd-empty-trash-test.c		\
	$(COMMON_FILES)
gsd_empty_trash_test_LDADD = $(HOUSEKEEPING_LIBS)
gsd_empty_trash_test_CFLAGS = $(HOUSEKEEPING_CFLAGS)

gsd_purge_temp_test_SOURCES =		\
	gsd-purge-temp-test.c		\
	$(COMMON_FILES)
gsd_purge_temp_test_LDADD = $(HOUSEKEEPING_LIBS)
gsd_purge_temp_test_CFLAGS = $(HOUSEKEEPING_CFLAGS)

libexec_PROGRAMS = gsd-housekeeping

gsd_housekeeping_SOURCES =		\
	main.c				\
	gsd-housekeeping-manager.c	\
	gsd-housekeeping-manager.h	\
	$(COMMON_FILES)

gsd_housekeeping_CPPFLAGS =					\
	-I$(top_srcdir)/data/					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	$(AM_CPPFLAGS)

gsd_housekeeping_CFLAGS =		\
	$(PLUGIN_CFLAGS)		\
	$(HOUSEKEEPING_CFLAGS)

gsd_housekeeping_LDADD =					\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(HOUSEKEEPING_LIBS)

desktopdir = $(sysconfdir)/xdg/autostart
desktop_in_files = org.gnome.SettingsDaemon.Housekeeping.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

org.gnome.SettingsDaemon.Housekeeping.desktop: $(desktop_in_files) Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST = $(desktop_in_files)

CLEANFILES = $(desktop_DATA)

DISTCLEANFILES = $(desktop_DATA)
