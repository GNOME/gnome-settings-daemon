plugin_name = color

check_PROGRAMS =			\
	gcm-self-test

gcm_self_test_CPPFLAGS = \
	-DTESTDATADIR=\""$(top_srcdir)/plugins/color/test-data"\" \
	$(AM_CPPFLAGS)

gcm_self_test_CFLAGS =			\
	$(COLOR_CFLAGS)			\
	$(PLUGIN_CFLAGS)		\
	$(AM_CFLAGS)

gcm_self_test_SOURCES =			\
	gcm-edid.c			\
	gcm-edid.h			\
	gsd-night-light.c		\
	gsd-night-light.h		\
	gsd-night-light-common.c	\
	gsd-night-light-common.h	\
	gnome-datetime-source.c		\
	gnome-datetime-source.h		\
	gcm-self-test.c

gcm_self_test_LDADD =			\
	$(COLOR_LIBS)			\
	$(SETTINGS_PLUGIN_LIBS)		\
	$(LIBM)

TESTS = gcm-self-test

BUILT_SOURCES = gschemas.compiled

CLEANFILES = gschemas.compiled

gschemas.compiled: $(top_builddir)/data/org.gnome.settings-daemon.plugins.color.gschema.xml
	$(AM_V_GEN) $(GLIB_COMPILE_SCHEMAS) --schema-file=$< --targetdir=$(builddir)

all-am: gschemas.compiled

libexec_PROGRAMS = gsd-color
gsd_color_SOURCES =			\
	main.c				\
	gnome-datetime-source.c		\
	gnome-datetime-source.h		\
	gcm-edid.c			\
	gcm-edid.h			\
	gsd-color-calibrate.c		\
	gsd-color-calibrate.h		\
	gsd-color-manager.c		\
	gsd-color-manager.h		\
	gsd-color-profiles.c		\
	gsd-color-profiles.h		\
	gsd-color-state.c		\
	gsd-color-state.h		\
	gsd-night-light.c		\
	gsd-night-light.h		\
	gsd-night-light-common.c	\
	gsd-night-light-common.h	\
	$(NULL)

gsd_color_CFLAGS =			\
	$(PLUGIN_CFLAGS)		\
	$(COLOR_CFLAGS)

gsd_color_CPPFLAGS =						\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	-DBINDIR=\"$(bindir)\"					\
	$(AM_CPPFLAGS)

gsd_color_LDADD =						\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(COLOR_LIBS)						\
	$(LIBM)

desktopdir = $(sysconfdir)/xdg/autostart
desktop_in_files = org.gnome.SettingsDaemon.Color.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

org.gnome.SettingsDaemon.Color.desktop: $(desktop_in_files) Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST = 					\
	$(desktop_in_files)			\
	test-data/Lenovo-T61-Internal.bin	\
	test-data/LG-L225W-External.bin

CLEANFILES += 				\
	$(desktop_DATA)

DISTCLEANFILES =			\
	$(desktop_DATA)
