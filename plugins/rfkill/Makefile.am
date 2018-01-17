plugin_name = rfkill

libexec_PROGRAMS = gsd-rfkill

gsd_rfkill_SOURCES =		\
	gsd-rfkill-manager.h	\
	gsd-rfkill-manager.c	\
	rfkill-glib.c		\
	rfkill-glib.h		\
	rfkill.h		\
	main.c

gsd_rfkill_CFLAGS =						\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(PLUGIN_CFLAGS)					\
	$(RFKILL_CFLAGS)

gsd_rfkill_LDADD =						\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(RFKILL_LIBS)

udevrulesdir = $(prefix)/lib/udev/rules.d
udevrules_DATA = 61-gnome-settings-daemon-rfkill.rules

unitdir = $(systemduserunitdir)
unit_in_files = gsd-rfkill.service.in
unit_DATA = gsd-rfkill.service

gsd-rfkill.service: $(unit_in_files) Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST = $(udevrules_DATA) $(unit_in_files)
CLEANFILES = $(unit_DATA)
DISTCLEANFILES = $(unit_DATA)
