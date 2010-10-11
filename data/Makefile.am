NULL =

apidir = $(includedir)/gnome-settings-daemon-$(GSD_API_VERSION)/gnome-settings-daemon
api_DATA = gsd-enums.h

gsettings_ENUM_NAMESPACE = org.gnome.settings-daemon
gsettings_ENUM_FILES = $(api_DATA)

gsettings_SCHEMAS =							\
	org.gnome.settings-daemon.peripherals.gschema.xml		\
	org.gnome.settings-daemon.plugins.gschema.xml			\
	org.gnome.settings-daemon.plugins.media-keys.gschema.xml	\
	org.gnome.settings-daemon.plugins.xsettings.gschema.xml		\
	org.gnome.settings-daemon.plugins.housekeeping.gschema.xml	\
	org.gnome.settings-daemon.plugins.xrandr.gschema.xml

@INTLTOOL_XML_NOMERGE_RULE@

@GSETTINGS_RULES@

convertdir = $(datadir)/GConf/gsettings
convert_DATA = gnome-settings-daemon.convert

@INTLTOOL_DESKTOP_RULE@
desktopdir = $(sysconfdir)/xdg/autostart
desktop_in_files = gnome-settings-daemon.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

servicedir = $(datadir)/dbus-1/services
service_in_files = org.gnome.SettingsDaemon.service.in
service_DATA = $(service_in_files:.service.in=.service)

org.gnome.SettingsDaemon.service: org.gnome.SettingsDaemon.service.in Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gnome-settings-daemon.pc

@INTLTOOL_XML_NOMERGE_RULE@

xmldir = @GNOME_KEYBINDINGS_KEYSDIR@
xml_in_files = 50-accessibility.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

EXTRA_DIST = 					\
	$(gsettings_SCHEMAS:.xml=.xml.in.in)	\
	$(service_in_files)			\
	$(desktop_in_files)			\
	$(xml_in_files)				\
	$(gsettings_ENUM_FILES)			\
	gnome-settings-daemon.pc.in		\
	$(NULL)

DISTCLEANFILES = 			\
	$(gsettings_SCHEMAS)		\
	$(service_DATA)			\
	$(desktop_DATA)			\
	$(xml_DATA)			\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in			\
	$(gsettings_SCHEMAS:.xml=.valid)
