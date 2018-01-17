plugin_name = sharing

libexec_PROGRAMS = gsd-sharing
gsd_sharing_SOURCES =					\
	gsd-sharing-manager.c				\
	gsd-sharing-manager.h				\
	gsd-sharing-enums.h				\
	main.c

gsd_sharing_CFLAGS =			\
	-I$(top_srcdir)/plugins/common	\
	$(SHARING_CFLAGS)		\
	$(PLUGIN_CFLAGS)
gsd_sharing_CPPFLAGS =						\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(AM_CPPFLAGS)
gsd_sharing_LDADD =					\
	$(top_builddir)/gnome-settings-daemon/libgsd.la \
	$(top_builddir)/plugins/common/libcommon.la	\
	$(SHARING_LIBS)

unitdir = $(systemduserunitdir)
unit_in_files = gsd-sharing.service.in
unit_DATA = gsd-sharing.service

gsd-sharing.service: $(unit_in_files) Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST = $(unit_in_files)
CLEANFILES = $(unit_DATA)
DISTCLEANFILES = $(unit_DATA)
