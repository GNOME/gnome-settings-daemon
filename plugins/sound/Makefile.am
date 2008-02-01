NULL =

SUBDIRS =		\
	libsounds	\
	$(NULL)

plugin_LTLIBRARIES = \
	libsound.la		\
	$(NULL)

libsound_la_SOURCES = 		\
	gsd-sound-plugin.h	\
	gsd-sound-plugin.c	\
	gsd-sound-manager.h	\
	gsd-sound-manager.c	\
	$(NULL)

libsound_la_CPPFLAGS = \
	-I$(top_srcdir)/src					\
	-I$(top_srcdir)/plugins/sounds/libsounds		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(AM_CPPFLAGS)

libsound_la_CFLAGS = \
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(GNOME_CFLAGS)			\
	$(AM_CFLAGS)

libsound_la_LDFLAGS = 		\
	$(GSD_PLUGIN_LDFLAGS)	\
	$(NULL)

libsound_la_LIBADD  = 		\
	$(top_builddir)/plugins/sound/libsounds/libsounds.la	\
	$(SETTINGS_PLUGIN_LIBS)	\
	$(NULL)

plugin_in_files = 		\
	sound.gnome-settings-plugin.in	\
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
