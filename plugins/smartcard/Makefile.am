plugin_name = smartcard
libsmartcard_headers = gsd-smartcard-manager.h \
		       gsd-smartcard-utils.h
dbus_built_sources = org.gnome.SettingsDaemon.Smartcard.c org.gnome.SettingsDaemon.Smartcard.h
enum_built_sources = gsd-smartcard-enum-types.h gsd-smartcard-enum-types.c
BUILT_SOURCES = $(dbus_built_sources) $(enum_built_sources)

libexec_PROGRAMS = gsd-test-smartcard

plugin_LTLIBRARIES = \
	libsmartcard.la

$(dbus_built_sources) : Makefile.am org.gnome.SettingsDaemon.Smartcard.xml
	$(AM_V_GEN) gdbus-codegen \
	--interface-prefix org.gnome.SettingsDaemon.Smartcard. \
	--c-namespace GsdSmartcardService \
	--c-generate-object-manager \
	--generate-c-code org.gnome.SettingsDaemon.Smartcard \
	$(srcdir)/org.gnome.SettingsDaemon.Smartcard.xml

gsd-smartcard-enum-types.h: gsd-smartcard-enum-types.h.in $(libsmartcard_headers)
	$(AM_V_GEN) $(GLIB_MKENUMS) --template $^ > $@

gsd-smartcard-enum-types.c: gsd-smartcard-enum-types.c.in $(libsmartcard_headers)
	$(AM_V_GEN) $(GLIB_MKENUMS) --template $^ > $@

gsd_test_smartcard_SOURCES =        \
	gsd-smartcard-service.c \
	gsd-smartcard-service.h \
	gsd-smartcard-manager.c \
	gsd-smartcard-utils.h \
	gsd-smartcard-utils.c   \
	test-smartcard.c

nodist_gsd_test_smartcard_SOURCES = \
	$(dbus_built_sources) \
	$(enum_built_sources)

gsd_test_smartcard_CPPFLAGS =                                   \
	-I$(top_srcdir)/data/                                   \
	-I$(top_srcdir)/gnome-settings-daemon                   \
	-I$(top_builddir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common                          \
	-DSYSCONFDIR=\""$(sysconfdir)"\"                        \
	-DLIBDIR=\""$(libdir)"\"                                \
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"      \
	-DGSD_SMARTCARD_MANAGER_NSS_DB=\""$(NSS_DATABASE)"\"    \
	$(AM_CPPFLAGS)

gsd_test_smartcard_CFLAGS =                                     \
	 $(PLUGIN_CFLAGS)                                       \
	 $(SETTINGS_PLUGIN_CFLAGS)                              \
	 $(MEDIA_KEYS_CFLAGS)                                   \
	 $(NSS_CFLAGS)                                          \
	 $(AM_CFLAGS)

gsd_test_smartcard_LDADD =                                      \
	$(top_builddir)/gnome-settings-daemon/libgsd.la         \
	$(top_builddir)/plugins/common/libcommon.la             \
	$(NSS_LIBS)                                             \
	$(SETTINGS_DAEMON_LIBS)                                 \
	$(SETTINGS_PLUGIN_LIBS)


libsmartcard_la_SOURCES = \
	$(libsmartcard_headers) \
	gsd-smartcard-manager.c \
	gsd-smartcard-plugin.c  \
	gsd-smartcard-service.h \
	gsd-smartcard-service.c \
	gsd-smartcard-utils.h \
	gsd-smartcard-utils.c

nodist_libsmartcard_la_SOURCES = \
	$(dbus_built_sources) \
	$(enum_built_sources)

libsmartcard_la_CPPFLAGS = \
	-I$(top_srcdir)/gnome-settings-daemon \
	-I$(top_builddir)/gnome-settings-daemon		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	-I$(top_srcdir)/plugins/common \
	-DSYSCONFDIR=\""$(sysconfdir)"\" \
	-DLIBDIR=\""$(libdir)"\" \
	-DGSD_SMARTCARD_MANAGER_NSS_DB=\""$(NSS_DATABASE)"\" \
	$(AM_CPPFLAGS)

libsmartcard_la_CFLAGS = \
	$(PLUGIN_CFLAGS)        \
	$(SETTINGS_PLUGIN_CFLAGS) \
	$(NSS_CFLAGS)        \
	$(AM_CFLAGS)

libsmartcard_la_LDFLAGS = \
	$(GSD_PLUGIN_LDFLAGS)

libsmartcard_la_LIBADD = \
	$(SETTINGS_PLUGIN_LIBS)        \
	$(NSS_LIBS)

@GSD_INTLTOOL_PLUGIN_RULE@

plugin_in_files = \
	smartcard.gnome-settings-plugin.in

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = \
	gsd-smartcard-enum-types.c.in \
	gsd-smartcard-enum-types.h.in \
	org.gnome.SettingsDaemon.Smartcard.xml \
	$(plugin_in_files)

CLEANFILES = \
	$(BUILT_SOURCES) \
	$(plugin_DATA)

DISTCLEANFILES = \
	$(BUILT_SOURCES) \
	$(plugin_DATA)
