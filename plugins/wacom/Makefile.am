plugin_name = wacom

plugin_LTLIBRARIES = libwacom.la

libwacom_la_SOURCES = 		\
	gsd-wacom-plugin.h	\
	gsd-wacom-plugin.c	\
	gsd-wacom-manager.h	\
	gsd-wacom-manager.c

libwacom_la_CPPFLAGS = \
	-I$(top_srcdir)/gnome-settings-daemon		\
	-I$(top_srcdir)/plugins/common/			\
	-I$(top_srcdir)/data/				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	-DLIBEXECDIR=\""$(libexecdir)"\" 		\
	$(AM_CPPFLAGS)

libwacom_la_CFLAGS = \
	$(PLUGIN_CFLAGS)		\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(AM_CFLAGS)

libwacom_la_LDFLAGS = 		\
	$(GSD_PLUGIN_LDFLAGS)

libwacom_la_LIBADD  = 							\
	$(top_builddir)/plugins/common/libcommon.la			\
	$(SETTINGS_PLUGIN_LIBS)						\
	$(XINPUT_LIBS)

plugin_in_files = wacom.gnome-settings-plugin.in

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = $(plugin_in_files)
CLEANFILES = $(plugin_DATA)
DISTCLEANFILES = $(plugin_DATA)

@GSD_INTLTOOL_PLUGIN_RULE@
