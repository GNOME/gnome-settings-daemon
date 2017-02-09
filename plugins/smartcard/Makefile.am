plugin_name = smartcard
libsmartcard_headers = gsd-smartcard-manager.h \
		       gsd-smartcard-utils.h
dbus_built_sources = org.gnome.SettingsDaemon.Smartcard.c org.gnome.SettingsDaemon.Smartcard.h
enum_built_sources = gsd-smartcard-enum-types.h gsd-smartcard-enum-types.c
BUILT_SOURCES = $(dbus_built_sources) $(enum_built_sources)

libexec_PROGRAMS = gsd-smartcard

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

gsd_smartcard_SOURCES =        \
	gsd-smartcard-service.c \
	gsd-smartcard-service.h \
	gsd-smartcard-manager.c \
	gsd-smartcard-manager.h \
	gsd-smartcard-utils.h \
	gsd-smartcard-utils.c   \
	main.c

nodist_gsd_smartcard_SOURCES = \
	$(dbus_built_sources) \
	$(enum_built_sources)

gsd_smartcard_CPPFLAGS =                                        \
	-I$(top_srcdir)/data/                                   \
	-I$(top_srcdir)/gnome-settings-daemon                   \
	-I$(top_builddir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common                          \
	-DSYSCONFDIR=\""$(sysconfdir)"\"                        \
	-DLIBDIR=\""$(libdir)"\"                                \
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"      \
	-DGSD_SMARTCARD_MANAGER_NSS_DB=\""$(NSS_DATABASE)"\"    \
	$(AM_CPPFLAGS)

gsd_smartcard_CFLAGS =                                          \
	 $(PLUGIN_CFLAGS)                                       \
	 $(SMARTCARD_CFLAGS)                                    \
	 $(AM_CFLAGS)

gsd_smartcard_LDADD =                                           \
	$(top_builddir)/gnome-settings-daemon/libgsd.la         \
	$(top_builddir)/plugins/common/libcommon.la             \
	$(SMARTCARD_LIBS)

desktopdir = $(sysconfdir)/xdg/autostart
desktop_in_files = org.gnome.SettingsDaemon.Smartcard.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

org.gnome.SettingsDaemon.Smartcard.desktop: $(desktop_in_files) Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST = \
	gsd-smartcard-enum-types.c.in \
	gsd-smartcard-enum-types.h.in \
	org.gnome.SettingsDaemon.Smartcard.xml \
	$(desktop_in_files)

CLEANFILES = \
	$(BUILT_SOURCES) \
	$(desktop_DATA)

DISTCLEANFILES = \
	$(BUILT_SOURCES) \
	$(desktop_DATA)
