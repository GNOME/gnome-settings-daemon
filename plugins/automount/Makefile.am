NULL =

noinst_PROGRAMS = 			\
	test-automount			\
	$(NULL)

test_automount_SOURCES = 		\
	test-automount.c		\
	gsd-automount-manager.h		\
	gsd-automount-manager.c		\
	nautilus-autorun.c		\
	nautilus-autorun.h		\
	$(NULL)

test_automount_CPPFLAGS = \
	-I$(top_srcdir)/gnome-settings-daemon		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	$(AM_CPPFLAGS)

test_automount_CFLAGS = \
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

test_automount_LDADD =			\
	$(top_builddir)/gnome-settings-daemon/libgsd-profile.la	\
	$(SETTINGS_PLUGIN_LIBS)		\
	$(X11_LIBS)			\
	$(NULL)

plugin_LTLIBRARIES = \
	libautomount.la		\
	$(NULL)

libautomount_la_SOURCES =		\
	gsd-automount-plugin.h		\
	gsd-automount-plugin.c		\
	gsd-automount-manager.h		\
	gsd-automount-manager.c		\
	nautilus-autorun.c		\
	nautilus-autorun.h		\
	nautilus-open-with-dialog.c	\
	nautilus-open-with-dialog.h	\
	$(NULL)

libautomount_la_CPPFLAGS = \
	-I$(top_srcdir)/gnome-settings-daemon		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	$(AM_CPPFLAGS)

libautomount_la_CFLAGS = \
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

libautomount_la_LDFLAGS =		\
	$(GSD_PLUGIN_LDFLAGS)		\
	$(NULL)

libautomount_la_LIBADD  =		\
	$(SETTINGS_PLUGIN_LIBS)		\
	$(NULL)

plugin_in_files =		\
	automount.gnome-settings-plugin.in	\
	$(NULL)

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = 			\
	$(plugin_in_files)	\
	$(NULL)

CLEANFILES = 			\
	$(plugin_DATA)		\
	$(NULL)

DISTCLEANFILES =		\
	$(plugin_DATA)		\
	$(NULL)

@GSD_INTLTOOL_PLUGIN_RULE@
