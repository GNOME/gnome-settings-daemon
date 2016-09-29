plugin_name = sharing

plugin_LTLIBRARIES = libsharing.la

libsharing_la_SOURCES =		\
	gsd-sharing-manager.c	\
	gsd-sharing-manager.h	\
	gsd-sharing-enums.h	\
	gsd-sharing-plugin.c

libsharing_la_CPPFLAGS =					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(AM_CPPFLAGS)

libsharing_la_CFLAGS =			\
	-I$(top_srcdir)/plugins/common	\
	$(SHARING_CFLAGS)		\
	$(PLUGIN_CFLAGS)		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

libsharing_la_LDFLAGS = $(GSD_PLUGIN_LDFLAGS)

libsharing_la_LIBADD  = $(SETTINGS_PLUGIN_LIBS) $(SHARING_LIBS)

libexec_PROGRAMS = gsd-sharing
gsd_sharing_SOURCES =					\
	gsd-sharing-manager.c				\
	gsd-sharing-manager.h				\
	main.c

gsd_sharing_CFLAGS = $(libsharing_la_CFLAGS)
gsd_sharing_CPPFLAGS = $(libsharing_la_CPPFLAGS)
gsd_sharing_LDADD =					\
	$(top_builddir)/gnome-settings-daemon/libgsd.la \
	$(SHARING_LIBS)					\
	$(SETTINGS_PLUGIN_LIBS)

plugin_in_files = sharing.gnome-settings-plugin.in

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = $(plugin_in_files)
CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)

@GSD_INTLTOOL_PLUGIN_RULE@
