plugin_name = rfkill

libexec_PROGRAMS = gsd-test-rfkill

gsd_test_rfkill_SOURCES =		\
	gsd-rfkill-manager.h	\
	gsd-rfkill-manager.c	\
	rfkill-glib.c		\
	rfkill-glib.h		\
	rfkill.h		\
	test-rfkill.c

gsd_test_rfkill_CFLAGS =					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon		\
	-I$(top_srcdir)/plugins/common				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(PLUGIN_CFLAGS)					\
	$(SETTINGS_PLUGIN_CFLAGS)				\
	$(AM_CFLAGS)

gsd_test_rfkill_LDADD =					\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(SETTINGS_PLUGIN_LIBS)

plugin_LTLIBRARIES = librfkill.la

librfkill_la_SOURCES = 		\
	gsd-rfkill-plugin.c	\
	gsd-rfkill-manager.h	\
	gsd-rfkill-manager.c	\
	rfkill-glib.c		\
	rfkill-glib.h		\
	rfkill.h

librfkill_la_CPPFLAGS = \
	-I$(top_srcdir)/gnome-settings-daemon		\
	-I$(top_builddir)/gnome-settings-daemon		\
	-I$(top_srcdir)/data/				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	-DLIBEXECDIR=\""$(libexecdir)"\" 		\
	$(AM_CPPFLAGS)

librfkill_la_CFLAGS = \
	$(PLUGIN_CFLAGS)		\
	$(RFKILL_CFLAGS)		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

librfkill_la_LDFLAGS = 		\
	$(GSD_PLUGIN_LDFLAGS)

librfkill_la_LIBADD  =						\
	$(RFKILL_LIBS)						\
	$(SETTINGS_PLUGIN_LIBS)

plugin_in_files = rfkill.gnome-settings-plugin.in

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

udevrulesdir = $(prefix)/lib/udev/rules.d
udevrules_DATA = 61-gnome-settings-daemon-rfkill.rules

EXTRA_DIST = $(plugin_in_files) $(udevrules_DATA)
CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)

@GSD_INTLTOOL_PLUGIN_RULE@
