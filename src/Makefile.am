NULL =

INCLUDES = \
	-DDATADIR=\""$(datadir)"\"				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	-DLIBEXECDIR=\""$(libexecdir)"\" 			\
	-DGNOME_SETTINGS_PLUGINDIR=\""$(libdir)/gnome-settings-daemon/plugins"\" \
	$(WARN_CFLAGS)						\
	$(DISABLE_DEPRECATED_CFLAGS)				\
	$(SETTINGS_DAEMON_CFLAGS)				\
	$(NULL)

install-exec-local: install-gnome-settings-daemon

uninstall-local:
	$(RM) $(DESTDIR)$(libexecdir)/gnome-settings-daemon-gnome-settings-daemon

install-gnome-settings-daemon:
	$(mkinstalldirs) $(DESTDIR)$(libexecdir)/gnome-settings-daemon
	$(LIBTOOL) --mode=install $(INSTALL_PROGRAM) gnome-settings-daemon $(DESTDIR)$(libexecdir)/gnome-settings-daemon

libexec_PROGRAMS = \
	$(NULL)

noinst_PROGRAMS= \
	gnome-settings-daemon
	$(NULL)

gnome-settings-manager-glue.h: gnome-settings-manager.xml Makefile.am
	dbus-binding-tool --prefix=gnome_settings_manager --mode=glib-server $< > $@

gnome-settings-client.h: gnome-settings-manager.xml Makefile.am
	dbus-binding-tool --prefix=gnome_settings_manager --mode=glib-client $< > $@

BUILT_SOURCES = 			\
	gnome-settings-manager-glue.h	\
	gnome-settings-client.h		\
	$(NULL)

Dbusapidir   = $(includedir)/gnome-settings-daemon-2.0/gnome-settings-daemon
Dbusapi_DATA = gnome-settings-client.h

gnome_settings_daemon_SOURCES =		\
	main.c				\
	gnome-settings-manager.c	\
	gnome-settings-manager.h	\
	gnome-settings-plugin.c		\
	gnome-settings-plugin.h		\
	gnome-settings-plugins-engine.c	\
	gnome-settings-plugins-engine.h	\
	gnome-settings-module.c		\
	gnome-settings-module.h		\
	$(NULL)

gnome_settings_daemon_CPPFLAGS = \
	$(AM_CPPFLAGS)			\
	$(NULL)

gnome_settings_daemon_CFLAGS = 		\
	$(AM_CFLAGS)			\
	$(AM_CFLAGS)			\
	$(NULL)

gnome_settings_daemon_LDFLAGS = 	\
	$(AM_LDFLAGS)			\
	$(NULL)

gnome_settings_daemon_LDADD = 		\
	$(SETTINGS_DAEMON_LIBS)		\
	$(NULL)

EXTRA_DIST = 				\
	$(Dbusapi_DATA)			\
	gnome-settings-manager.xml	\
	$(NULL)

CLEANFILES = 				\
	$(BUILT_SOURCES)		\
	$(NULL)


# vim: ts=8
