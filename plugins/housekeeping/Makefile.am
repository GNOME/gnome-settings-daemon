plugin_LTLIBRARIES = libhousekeeping.la

libhousekeeping_la_SOURCES = 		\
	gsd-ldsm-dialog.c	\
	gsd-ldsm-dialog.h	\
	gsd-ldsm-trash-empty.c	\
	gsd-ldsm-trash-empty.h	\
	gsd-disk-space.c		\
	gsd-disk-space.h		\
	gsd-housekeeping-manager.c	\
	gsd-housekeeping-manager.h	\
	gsd-housekeeping-plugin.c	\
	gsd-housekeeping-plugin.h

libhousekeeping_la_CPPFLAGS = 					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(AM_CPPFLAGS)

libhousekeeping_la_CFLAGS = 		\
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

DISTCLEANFILES = (plugin_DATA)

@GSD_INTLTOOL_PLUGIN_RULE@
