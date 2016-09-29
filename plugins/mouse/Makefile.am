plugin_name = mouse

desktopdir = $(sysconfdir)/xdg/autostart
desktop_in_files = org.gnome.SettingsDaemon.Mouse.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

org.gnome.SettingsDaemon.Mouse.desktop: $(desktop_in_files) Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

libexec_PROGRAMS = gsd-locate-pointer

gsd_locate_pointer_SOURCES = 	\
	gsd-locate-pointer.h	\
	gsd-locate-pointer.c	\
	gsd-timeline.h		\
	gsd-timeline.c

gsd_locate_pointer_CFLAGS = $(MOUSE_LOCATE_POINTER_CFLAGS)

gsd_locate_pointer_LDADD  = $(MOUSE_LOCATE_POINTER_LIBS) $(LIBM)

libexec_PROGRAMS += gsd-mouse

gsd_mouse_SOURCES =			\
	main.c				\
	gsd-mouse-manager.c		\
	gsd-mouse-manager.h

gsd_mouse_CPPFLAGS =						\
	-I$(top_srcdir)/data/					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	$(AM_CPPFLAGS)

gsd_mouse_CFLAGS =		\
	$(PLUGIN_CFLAGS)	\
	$(MOUSE_CFLAGS)

gsd_mouse_LDADD =						\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(MOUSE_LIBS)						\
	$(LIBM)

EXTRA_DIST = $(desktop_in_files)
CLEANFILES = $(desktop_DATA)
DISTCLEANFILES = $(desktop_DATA)
