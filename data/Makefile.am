NULL =

@INTLTOOL_SCHEMAS_RULE@

schemasdir = @GCONF_SCHEMA_FILE_DIR@
schemas_in_files = gnome-settings-daemon.schemas.in
schemas_DATA = $(schemas_in_files:.schemas.in=.schemas)

servicedir = $(datadir)/dbus-1/services
service_in_files = org.gnome.SettingsDaemon.service.in
service_DATA = $(service_in_files:.service.in=.service)

org.gnome.SettingsDaemon.service: org.gnome.SettingsDaemon.service.in Makefile
	@sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gnome-settings-daemon.pc

EXTRA_DIST = 				\
	$(schemas_in_files)		\
	$(service_in_files)		\
	gnome-settings-daemon.pc.in	\
	$(NULL)

DISTCLEANFILES = 			\
	$(schemas_DATA)			\
	$(service_DATA)			\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in

if GCONF_SCHEMAS_INSTALL
# don't do this if we are building in eg. rpm
install-data-local: $(schemas_DATA)
	if test -z "$(DESTDIR)" ; then \
		for p in $(schemas_DATA) ; do \
			GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(top_builddir)/data/$$p ; \
		done \
	fi
else
install-data-local:
endif
