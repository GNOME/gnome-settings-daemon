plugin_name = power

plugin_LTLIBRARIES =					\
	libpower.la

libpower_la_SOURCES = 					\
	gsd-power-manager.c				\
	gsd-power-manager.h				\
	gsd-power-plugin.c				\
	gsd-power-plugin.h

libpower_la_CPPFLAGS = \
	-I$(top_srcdir)/data/				\
	-I$(top_srcdir)/gnome-settings-daemon		\
	-I$(top_srcdir)/plugins/common			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	-DGTKBUILDERDIR=\""$(pkgdatadir)"\"		\
	-DSBINDIR=\"$(sbindir)\"			\
	$(AM_CPPFLAGS)

libpower_la_CFLAGS = \
	$(PLUGIN_CFLAGS)				\
	$(KEYRING_CFLAGS)				\
	$(PULSE_CFLAGS)					\
	$(UPOWER_CFLAGS)				\
	$(SETTINGS_PLUGIN_CFLAGS)			\
	$(AM_CFLAGS)

libpower_la_LDFLAGS = 					\
	$(GSD_PLUGIN_LDFLAGS)

libpower_la_LIBADD  = 					\
	$(top_builddir)/plugins/common/libcommon.la	\
	$(UPOWER_LIBS)					\
	$(PULSE_LIBS)					\
	$(KEYRING_LIBS)					\
	$(SETTINGS_PLUGIN_LIBS)

plugin_in_files = 					\
	power.gnome-settings-plugin.in

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = 						\
	$(plugin_in_files)

clean-local:
	rm -f *~

CLEANFILES = 						\
	$(plugin_DATA)

DISTCLEANFILES =					\
	$(plugin_DATA)

@GSD_INTLTOOL_PLUGIN_RULE@
