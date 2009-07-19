NULL =

@INTLTOOL_SCHEMAS_RULE@

schemasdir = $(GCONF_SCHEMA_FILE_DIR)
schemas_in_files = 						\
	gnome-settings-daemon.schemas.in			\
	apps_gnome_settings_daemon_housekeeping.schemas.in	\
	apps_gnome_settings_daemon_keybindings.schemas.in	\
	desktop_gnome_font_rendering.schemas.in			\
	desktop_gnome_keybindings.schemas.in			\
	apps_gnome_settings_daemon_xrandr.schemas.in		\
	desktop_gnome_peripherals_touchpad.schemas.in		\
	$(NULL)

schemas_DATA = $(schemas_in_files:.schemas.in=.schemas)

@INTLTOOL_DESKTOP_RULE@
desktopdir = $(sysconfdir)/xdg/autostart
desktop_in_files = gnome-settings-daemon.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

servicedir = $(datadir)/dbus-1/services
service_in_files = org.gnome.SettingsDaemon.service.in
service_DATA = $(service_in_files:.service.in=.service)

org.gnome.SettingsDaemon.service: org.gnome.SettingsDaemon.service.in Makefile
	@sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gnome-settings-daemon.pc

@INTLTOOL_XML_NOMERGE_RULE@

xmldir = @GNOME_KEYBINDINGS_KEYSDIR@
xml_in_files = 50-accessibility.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

EXTRA_DIST = 				\
	$(schemas_in_files)		\
	$(service_in_files)		\
	$(desktop_in_files)		\
	$(xml_in_files)			\
	gnome-settings-daemon.pc.in	\
	$(NULL)

DISTCLEANFILES = 			\
	$(schemas_DATA)			\
	$(service_DATA)			\
	$(desktop_DATA)			\
	$(xml_DATA)			\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in

if GCONF_SCHEMAS_INSTALL
# don't do this if we are building in eg. rpm
install-data-local: $(schemas_DATA)
	if test -z "$(DESTDIR)" ; then \
		for p in $(schemas_DATA) ; do \
			GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(top_builddir)/data/$$p 2>&1 > /dev/null; \
		done \
	fi
else
install-data-local:
endif
