NULL =

apidir = $(includedir)/gnome-settings-daemon-$(GSD_API_VERSION)/gnome-settings-daemon
api_DATA = gsd-enums.h

gsettings_ENUM_NAMESPACE = org.gnome.settings-daemon
gsettings_ENUM_FILES = $(top_srcdir)/data/$(api_DATA)

gsettings_SCHEMAS =							\
	org.gnome.settings-daemon.peripherals.gschema.xml		\
	org.gnome.settings-daemon.plugins.gschema.xml			\
	org.gnome.settings-daemon.plugins.power.gschema.xml		\
	org.gnome.settings-daemon.plugins.color.gschema.xml		\
	org.gnome.settings-daemon.plugins.media-keys.gschema.xml	\
	org.gnome.settings-daemon.plugins.sharing.gschema.xml		\
	org.gnome.settings-daemon.plugins.xsettings.gschema.xml		\
	org.gnome.settings-daemon.plugins.housekeeping.gschema.xml	\
	org.gnome.settings-daemon.peripherals.wacom.gschema.xml

@INTLTOOL_XML_NOMERGE_RULE@

@GSETTINGS_RULES@

convertdir = $(datadir)/GConf/gsettings
convert_DATA = gnome-settings-daemon.convert

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gnome-settings-daemon.pc

@INTLTOOL_XML_NOMERGE_RULE@

EXTRA_DIST = 					\
	$(convert_DATA)				\
	$(gsettings_SCHEMAS:.xml=.xml.in.in)	\
	$(gsettings_ENUM_FILES)			\
	gnome-settings-daemon.pc.in		\
	$(api_DATA)				\
	$(NULL)

DISTCLEANFILES = 			\
	$(gsettings_SCHEMAS)		\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in			\
	$(gsettings_SCHEMAS:.xml=.valid)

