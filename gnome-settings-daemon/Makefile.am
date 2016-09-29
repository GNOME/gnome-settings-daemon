NULL =
BUILT_SOURCES = $(NULL)

AM_CPPFLAGS = \
	-DDATADIR=\""$(datadir)"\"				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	-DLIBEXECDIR=\""$(libexecdir)"\" 			\
	$(WARN_CFLAGS)						\
	$(DISABLE_DEPRECATED_CFLAGS)				\
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

libgsd_la_CFLAGS = $(HELPER_CFLAGS)

libgsd_la_LIBADD =		\
	$(HELPER_LIBS)		\
	$(WAYLAND_LIBS)		\
	$(NULL)

libgsd_la_LDFLAGS =		\
	-export-dynamic		\
	-avoid-version		\
	-no-undefined		\
	$(NULL)

EXTRA_DIST = \
	org.gnome.SessionManager.xml        \
	org.gnome.ScreenSaver.xml           \
	org.gnome.Shell.xml                 \
	$(NULL)

CLEANFILES = $(gsd_SCRIPTS)

# vim: ts=8
