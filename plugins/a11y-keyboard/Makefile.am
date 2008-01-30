NULL =

modules_flags = -export_dynamic -avoid-version -module -no-undefined

plugindir = $(libdir)/gnome-settings-daemon/plugins/a11y-keyboard

INCLUDES = \
	-I$(top_srcdir)/src		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"\
	$(SETTINGS_PLUGIN_CFLAGS)			\
	$(GNOME_CFLAGS)			\
	$(NULL)

plugin_LTLIBRARIES = \
	liba11y-keyboard.la		\
	$(NULL)

liba11y_keyboard_la_SOURCES = 		\
	gsd-a11y-keyboard-plugin.h	\
	gsd-a11y-keyboard-plugin.c	\
	gsd-a11y-keyboard-manager.h	\
	gsd-a11y-keyboard-manager.c	\
	$(NULL)

liba11y_keyboard_la_LDFLAGS = 		\
	$(PLUGIN_LIBTOOL_FLAGS)		\
	$(modules_flags)		\
	$(NULL)

liba11y_keyboard_la_LIBADD  = 		\
	$(SETTINGS_PLUGIN_LIBS)		\
	$(XF86MISC_LIBS)		\
	$(GNOME_LIBS)			\
	$(NULL)

liba11y_keyboard_la_CFLAGS = 		\
	$(NULL)

plugin_in_files = 		\
	a11y-keyboard.gnome-settings-plugin.in	\
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
