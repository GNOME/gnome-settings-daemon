plugin_name = dummy

plugin_LTLIBRARIES = \
	libdummy.la

libdummy_la_SOURCES = 		\
	gsd-dummy-manager.c	\
	gsd-dummy-manager.h	\
	gsd-dummy-plugin.c

libdummy_la_CPPFLAGS = \
	-I$(top_srcdir)/gnome-settings-daemon		\
	-I$(top_builddir)/gnome-settings-daemon		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	$(AM_CPPFLAGS)

libdummy_la_CFLAGS = \
	$(PLUGIN_CFLAGS)		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

libdummy_la_LDFLAGS = 		\
	$(GSD_PLUGIN_LDFLAGS)

libdummy_la_LIBADD  = 		\
	$(SETTINGS_PLUGIN_LIBS)

libexec_PROGRAMS = gsd-test-dummy
gsd_test_dummy_SOURCES =		\
	test-dummy.c			\
	gsd-dummy-manager.c		\
	gsd-dummy-manager.h

gsd_test_dummy_CFLAGS = $(libdummy_la_CFLAGS)
gsd_test_dummy_CPPFLAGS =		\
	-I$(top_srcdir)/plugins/common	\
	$(libdummy_la_CPPFLAGS)
gsd_test_dummy_LDADD = $(libdummy_la_LIBADD) $(top_builddir)/gnome-settings-daemon/libgsd.la

plugin_in_files = 		\
	dummy.gnome-settings-plugin.in

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = 			\
	$(plugin_in_files)

CLEANFILES = 			\
	$(plugin_DATA)

DISTCLEANFILES =		\
	$(plugin_DATA)

@GSD_INTLTOOL_PLUGIN_RULE@

# override to _not_ install the test plugin
# do not copy into your plugin
install-pluginDATA:
install-pluginLTLIBRARIES:
