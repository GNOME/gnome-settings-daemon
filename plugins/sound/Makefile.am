NULL =

SUBDIRS =		\
	libsounds	\
	$(NULL)

modules_flags = -export_dynamic -avoid-version -module -no-undefined

plugindir = $(libdir)/gnome-settings-daemon/plugins/sound

INCLUDES = \
	-I$(top_srcdir)/src		\
	-I$(top_srcdir)/plugins/sounds/libsounds	\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"\
	$(SETTINGS_PLUGIN_CFLAGS)			\
	$(GNOME_CFLAGS)					\
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

libsound_la_LDFLAGS = 		\
	$(PLUGIN_LIBTOOL_FLAGS)	\
	$(modules_flags)	\
	$(NULL)

libsound_la_LIBADD  = 		\
	$(top_builddir)/plugins/sound/libsounds/libsounds.la	\
	$(SETTINGS_PLUGIN_LIBS)	\
	$(NULL)

libsound_la_CFLAGS = 		\
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
