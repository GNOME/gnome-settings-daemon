plugin_name = common

noinst_LTLIBRARIES = libcommon.la

libcommon_la_SOURCES = \
	$(dbus_built_sources)	\
	gsd-keygrab.c		\
	gsd-keygrab.h		\
	gsd-input-helper.c	\
	gsd-input-helper.h

libcommon_la_CPPFLAGS = \
	$(AM_CPPFLAGS)

libcommon_la_CFLAGS = \
	$(PLUGIN_CFLAGS)		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(COMMON_CFLAGS)		\
	$(AM_CFLAGS)

libcommon_la_LDFLAGS = \
	$(GSD_PLUGIN_LDFLAGS)

libcommon_la_LIBADD  = \
	$(SETTINGS_PLUGIN_LIBS)		\
	$(COMMON_LIBS)

libexec_PROGRAMS = gsd-test-input-helper

gsd_test_input_helper_SOURCES = test-input-helper.c
gsd_test_input_helper_LDADD = libcommon.la
gsd_test_input_helper_CFLAGS = $(libcommon_la_CFLAGS)

noinst_PROGRAMS = test-egg-key-parsing

test_egg_key_parsing_SOURCES = test-egg-key-parsing.c
test_egg_key_parsing_LDADD = libcommon.la $(COMMON_LIBS)
test_egg_key_parsing_CFLAGS = $(libcommon_la_CFLAGS)

scriptsdir = $(datadir)/gnome-settings-daemon-@GSD_API_VERSION@
scripts_DATA = input-device-example.sh

dbus_built_sources = shell-key-grabber.c shell-key-grabber.h
shell-key-grabber.c: shell-key-grabber.h
shell-key-grabber.h: Makefile.am org.gnome.ShellKeyGrabber.xml
	gdbus-codegen --interface-prefix org.gnome.		\
	              --generate-c-code shell-key-grabber	\
	              --c-namespace Shell			\
	              org.gnome.ShellKeyGrabber.xml

EXTRA_DIST = $(scripts_DATA) test-plugin.h org.gnome.Shell.xml
CLEANFILES = $(dbus_built_sources)
