NULL =
BUILT_SOURCES = $(NULL)

AM_CPPFLAGS = \
	-DDATADIR=\""$(datadir)"\"				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	-DLIBEXECDIR=\""$(libexecdir)"\" 			\
	-DGNOME_SETTINGS_PLUGINDIR=\""$(plugindir)"\"		\
	$(WARN_CFLAGS)						\
	$(DISABLE_DEPRECATED_CFLAGS)				\
	$(SETTINGS_DAEMON_CFLAGS)				\
	$(LIBNOTIFY_CFLAGS)					\
	$(GNOME_DESKTOP_CFLAGS)					\
	$(WAYLAND_CFLAGS)					\
	$(NULL)

privlibdir = $(pkglibdir)-$(GSD_API_VERSION)

privlib_LTLIBRARIES =		\
	libgsd.la		\
	$(NULL)

session_manager_dbus_built_sources = gsd-session-manager-glue.c gsd-session-manager-glue.h
BUILT_SOURCES += $(session_manager_dbus_built_sources)

$(session_manager_dbus_built_sources) : Makefile.am org.gnome.SessionManager.xml
	gdbus-codegen							\
		--interface-prefix org.gnome.SessionManager.		\
		--generate-c-code gsd-session-manager-glue		\
		--c-namespace Gsd					\
		--annotate "org.gnome.SessionManager"			\
		           "org.gtk.GDBus.C.Name" SessionManager	\
		$(srcdir)/org.gnome.SessionManager.xml

screen_saver_dbus_built_sources = gsd-screen-saver-glue.c gsd-screen-saver-glue.h

BUILT_SOURCES += $(screen_saver_dbus_built_sources)

$(screen_saver_dbus_built_sources) : Makefile.am org.gnome.ScreenSaver.xml
	gdbus-codegen						\
		--interface-prefix org.gnome.ScreenSaver.	\
		--generate-c-code gsd-screen-saver-glue		\
		--c-namespace Gsd				\
		--annotate "org.gnome.ScreenSaver"		\
		           "org.gtk.GDBus.C.Name" ScreenSaver	\
		$(srcdir)/org.gnome.ScreenSaver.xml

shell_dbus_built_sources = gsd-shell-glue.c gsd-shell-glue.h

BUILT_SOURCES += $(shell_dbus_built_sources)

$(shell_dbus_built_sources) : Makefile.am org.gnome.Shell.xml
	gdbus-codegen						\
		--interface-prefix org.gnome.Shell.		\
		--generate-c-code gsd-shell-glue		\
		--c-namespace Gsd				\
		--annotate "org.gnome.Shell"			\
			   "org.gtk.GDBus.C.Name" Shell		\
		$(srcdir)/org.gnome.Shell.xml

libgsd_la_SOURCES =		\
	$(session_manager_dbus_built_sources) \
	$(screen_saver_dbus_built_sources) \
	$(shell_dbus_built_sources)	\
	gnome-settings-profile.c	\
	gnome-settings-profile.h	\
	gnome-settings-bus.c	\
	gnome-settings-bus.h	\
	$(NULL)

libgsd_la_CPPFLAGS = 		\
	$(AM_CPPFLAGS)			\
	$(DISABLE_DEPRECATED_CFLAGS)	\
	$(NULL)

libgsd_la_CFLAGS =		\
	$(NULL)

libgsd_la_LIBADD =		\
	$(SETTINGS_DAEMON_LIBS)		\
	$(GIOUNIX_LIBS)		\
	$(WAYLAND_LIBS)		\
	$(NULL)

libgsd_la_LDFLAGS =		\
	-export-dynamic		\
	-avoid-version		\
	-no-undefined		\
	$(NULL)

gsddir = $(libexecdir)

gsd_PROGRAMS = \
	gnome-settings-daemon

gsd_SCRIPTS = \
	gnome-settings-daemon-localeexec

EXTRA_DIST = \
	gnome-settings-daemon-localeexec.in \
	org.gnome.SessionManager.xml        \
	org.gnome.ScreenSaver.xml           \
	org.gnome.Shell.xml                 \
	$(NULL)

gnome-settings-daemon-localeexec: gnome-settings-daemon-localeexec.in
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" -e "s|\@prefix\@|$(prefix)|"  $< > $@.tmp && mv $@.tmp $@

apidir   = $(includedir)/gnome-settings-daemon-$(GSD_API_VERSION)/gnome-settings-daemon
api_DATA = 				\
	gnome-settings-plugin.h

gnome_settings_daemon_SOURCES =		\
	main.c				\
	gnome-settings-manager.c	\
	gnome-settings-manager.h	\
	gnome-settings-plugin.c		\
	gnome-settings-plugin.h		\
	gnome-settings-plugin-info.c	\
	gnome-settings-plugin-info.h	\
	gnome-settings-module.c		\
	gnome-settings-module.h		\
	$(NULL)

gnome_settings_daemon_CPPFLAGS = \
	$(AM_CPPFLAGS)

gnome_settings_daemon_CFLAGS = 		\
	$(AM_CFLAGS)

gnome_settings_daemon_LDFLAGS = 	\
	$(AM_LDFLAGS)

gnome_settings_daemon_LDADD = 		\
	libgsd.la		\
	$(SETTINGS_DAEMON_LIBS)		\
	$(LIBNOTIFY_LIBS)		\
	$(GNOME_DESKTOP_LIBS)		\
	$(NULL)

CLEANFILES = $(gsd_SCRIPTS)

# vim: ts=8
