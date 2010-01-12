noinst_LTLIBRARIES =						\
	libhal-glib.la

libhal_glib_la_SOURCES =					\
	egg-dbus-proxy.h					\
	egg-dbus-proxy.c					\
	egg-dbus-monitor.h					\
	egg-dbus-monitor.c					\
	egg-debug.h						\
	egg-debug.c						\
	hal-marshal.h						\
	hal-marshal.c						\
	hal-device.c						\
	hal-device.h						\
	hal-manager.c						\
	hal-manager.h						\
	hal-device-store.c					\
	hal-device-store.h					\
	hal-device-power.c					\
	hal-device-power.h

libhal_glib_la_CFLAGS = \
        $(SETTINGS_PLUGIN_CFLAGS)       \
        $(GST_CFLAGS)                   \
        $(AM_CFLAGS)			\
	-DEGG_LOG_FILE=\""~/dalston.log\""                      \
        -DEGG_VERBOSE="\"DALSTON_VERBOSE\""                     \
        -DEGG_LOGGING="\"DALSTON_LOGGING\""                     \
        -DEGG_CONSOLE="\"DALSTON_CONSOLE\""                     \
        -DPACKAGE_DATA_DIR=\""$(datadir)"\"                     \
        -DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\"


libhal_glib_la_LIBADD =						\
	$(GLIB_LIBS)						\
	$(DBUS_GLIB_LIBS)

EXTRA_DIST =							\
	hal-marshal.list

BUILT_SOURCES = 						\
	hal-marshal.c						\
	hal-marshal.h

hal-marshal.c: hal-marshal.list
	echo "#include \"hal-marshal.h\"" > $@ && \
	glib-genmarshal $< --prefix=hal_marshal --body >> $@

hal-marshal.h: hal-marshal.list
	glib-genmarshal $< --prefix=hal_marshal --header > $@

clean-local:
	rm -f *~
	rm -f hal-marshal.c hal-marshal.h

CLEANFILES = $(BUILT_SOURCES)

