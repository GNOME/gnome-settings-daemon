NULL =

plugin_LTLIBRARIES = \
	libdefault-editor.la		\
	$(NULL)

libdefault_editor_la_SOURCES = 		\
	gsd-default-editor-plugin.h	\
	gsd-default-editor-plugin.c	\
	gsd-default-editor-manager.h	\
	gsd-default-editor-manager.c	\
	$(NULL)

libdefault_editor_la_CPPFLAGS = \
	-I$(top_srcdir)/src		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	$(AM_CPPFLAGS)

libdefault_editor_la_CFLAGS = \
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

libdefault_editor_la_LDFLAGS = 		\
	$(GSD_PLUGIN_LDFLAGS)	\
	$(NULL)

libdefault_editor_la_LIBADD  = 		\
	$(SETTINGS_PLUGIN_LIBS)	\
	$(NULL)

plugin_in_files = 		\
	default-editor.gnome-settings-plugin.in	\
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
