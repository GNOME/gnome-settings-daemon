NULL =

SUBDIRS =		\
	libsounds	\
	$(NULL)

modules_flags = -export_dynamic -avoid-version -module -no-undefined

plugindir = $(libdir)/gnome-settings-daemon/plugins/sound

INCLUDES = \
	-I$(top_srcdir)/src		\
	-I$(top_builddir)/plugins/sounds/libsounds	\
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
	$(SETTINGS_PLUGIN_LIBS)	\
	$(top_builddir)/plugins/sound/libsounds/libsounds.a	\
	$(NULL)

libsound_la_CFLAGS = 		\
	$(NULL)

plugin_in_files = 		\
	sound.gnome-settings-plugin.desktop.in	\
	$(NULL)

%.gnome-settings-plugin: %.gnome-settings-plugin.desktop.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.desktop.in=.gnome-settings-plugin)

EXTRA_DIST = 			\
	$(plugin_in_files)	\
	$(NULL)

CLEANFILES = 			\
	$(plugin_DATA)		\
	$(NULL)

DISTCLEANFILES =		\
	$(plugin_DATA)		\
	$(NULL)

