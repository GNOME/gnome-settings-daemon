plugin_name = sound

libexec_PROGRAMS = gsd-sound

gsd_sound_SOURCES =		\
	gsd-sound-manager.h	\
	gsd-sound-manager.c	\
	main.c

gsd_sound_CFLAGS =						\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon		\
	-I$(top_srcdir)/plugins/common				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(SOUND_CFLAGS)						\
	$(PLUGIN_CFLAGS)

gsd_sound_LDADD =						\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(SOUND_LIBS)

unitdir = $(systemduserunitdir)
unit_in_files = gsd-sound.service.in
unit_DATA = gsd-sound.service

gsd-sound.service: $(unit_in_files) Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST = \
	$(unit_in_Files)

CLEANFILES = \
	$(unit_DATA)

DISTCLEANFILES = \
	$(unit_DATA)
