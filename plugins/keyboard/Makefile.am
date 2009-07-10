NULL =

plugin_LTLIBRARIES = \
	libkeyboard.la		\
	$(NULL)

uidir   = $(pkgdatadir)
ui_DATA = modmap-dialog.ui

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

libkeyboard_la_CPPFLAGS = \
	-I$(top_srcdir)/gnome-settings-daemon		\
	-DDATADIR=\""$(pkgdatadir)"\"	\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	$(AM_CPPFLAGS)

libkeyboard_la_CFLAGS = \
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(LIBGNOMEKBD_CFLAGS)		\
	$(AM_CFLAGS)

libkeyboard_la_LDFLAGS = 	\
	$(GSD_PLUGIN_LDFLAGS)	\
	$(NULL)

libkeyboard_la_LIBADD  = 	\
	$(SETTINGS_PLUGIN_LIBS)	\
	$(XF86MISC_LIBS)	\
	$(LIBGNOMEKBD_LIBS)	\
	$(NULL)

plugin_in_files = 		\
	keyboard.gnome-settings-plugin.in	\
	$(NULL)

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = 			\
	$(plugin_in_files)	\
	$(ui_DATA)		\
	$(NULL)

CLEANFILES = 			\
	$(plugin_DATA)		\
	$(NULL)

DISTCLEANFILES =		\
	$(plugin_DATA)		\
	$(NULL)

@GSD_INTLTOOL_PLUGIN_RULE@
