NULL =

modules_flags = -export_dynamic -avoid-version -module -no-undefined

plugindir = $(libdir)/gnome-settings-daemon/plugins/keyboard

INCLUDES = \
	-I$(top_srcdir)/src		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"\
	-DDATADIR=\""$(plugindir)"\"\
	$(SETTINGS_PLUGIN_CFLAGS)			\
	$(LIBGNOMEKBD_CFLAGS)				\
	$(NULL)

plugin_LTLIBRARIES = \
	libkeyboard.la		\
	$(NULL)

Gladedir   = $(plugindir)
Glade_DATA = modmap-dialog.glade

libkeyboard_la_SOURCES = 	\
	gsd-keyboard-plugin.h	\
	gsd-keyboard-plugin.c	\
	gsd-keyboard-manager.h	\
	gsd-keyboard-manager.c	\
	gsd-keyboard-xkb.h	\
	gsd-keyboard-xkb.c	\
	gsd-xmodmap.h		\
	gsd-xmodmap.c		\
	delayed-dialog.h	\
	delayed-dialog.c	\
	$(NULL)

libkeyboard_la_LDFLAGS = 	\
	$(PLUGIN_LIBTOOL_FLAGS)	\
	$(modules_flags)	\
	$(NULL)

libkeyboard_la_LIBADD  = 	\
	$(SETTINGS_PLUGIN_LIBS)	\
	$(XF86MISC_LIBS)	\
	$(LIBGNOMEKBD_LIBS)	\
	$(NULL)

libkeyboard_la_CFLAGS = 		\
	$(NULL)

plugin_in_files = 		\
	keyboard.gnome-settings-plugin.in	\
	$(NULL)

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = 			\
	$(plugin_in_files)	\
	$(Glade_DATA)		\
	$(NULL)

CLEANFILES = 			\
	$(plugin_DATA)		\
	$(NULL)

DISTCLEANFILES =		\
	$(plugin_DATA)		\
	$(NULL)

@GSD_INTLTOOL_PLUGIN_RULE@
