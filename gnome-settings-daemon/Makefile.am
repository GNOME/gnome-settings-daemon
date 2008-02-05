INCLUDES = \
	-DDATADIR=\""$(datadir)"\"				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	-DLIBEXECDIR=\""$(libexecdir)"\" 			\
	-DGNOME_SETTINGS_PLUGINDIR=\""$(plugindir)"\"		\
	$(WARN_CFLAGS)						\
	$(DISABLE_DEPRECATED_CFLAGS)				\
	$(SETTINGS_DAEMON_CFLAGS)

gsddir = $(libexecdir)

gsd_PROGRAMS = \
	gnome-settings-daemon

gnome-settings-manager-glue.h: gnome-settings-manager.xml Makefile.am
	dbus-binding-tool --prefix=gnome_settings_manager --mode=glib-server $< > $@

gnome-settings-client.h: gnome-settings-manager.xml Makefile.am
	dbus-binding-tool --prefix=gnome_settings_manager --mode=glib-client $< > $@

BUILT_SOURCES = 			\
	gnome-settings-manager-glue.h	\
	gnome-settings-client.h

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
	gnome-settings-module.h

gnome_settings_daemon_CPPFLAGS = \
	$(AM_CPPFLAGS)

gnome_settings_daemon_CFLAGS = 		\
	$(AM_CFLAGS)

gnome_settings_daemon_LDFLAGS = 	\
	$(AM_LDFLAGS)

gnome_settings_daemon_LDADD = 		\
	$(SETTINGS_DAEMON_LIBS)

EXTRA_DIST = 				\
	$(Dbusapi_DATA)			\
	gnome-settings-manager.xml

CLEANFILES = 				\
	$(BUILT_SOURCES)


# vim: ts=8
