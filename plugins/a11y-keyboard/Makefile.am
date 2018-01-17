NULL =

plugin_name = a11y-keyboard

libexec_PROGRAMS = gsd-a11y-keyboard

gsd_a11y_keyboard_SOURCES =			\
	gsd-a11y-keyboard-manager.h		\
	gsd-a11y-keyboard-manager.c		\
	main.c

gsd_a11y_keyboard_CFLAGS =					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(PLUGIN_CFLAGS)					\
	$(A11Y_KEYBOARD_CFLAGS)

gsd_a11y_keyboard_LDADD =					\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(A11Y_KEYBOARD_LIBS)

unitdir = $(systemduserunitdir)
unit_in_files = gsd-a11y-keyboard.service.in
unit_DATA = gsd-a11y-keyboard.service

gsd-a11y-keyboard.service: $(unit_in_files) Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST = 			\
	$(unit_in_files)	\
	$(NULL)

CLEANFILES = 			\
	$(unit_DATA)		\
	$(NULL)

DISTCLEANFILES =		\
	$(unit_DATA)		\
	$(NULL)
