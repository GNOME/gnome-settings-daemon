plugin_name = dummy

libexec_PROGRAMS = gsd-dummy
gsd_dummy_SOURCES =			\
	main.c				\
	gsd-dummy-manager.c		\
	gsd-dummy-manager.h

gsd_dummy_CFLAGS =			\
	$(PLUGIN_CFLAGS)		\
	$(DUMMY_CFLAGS)

gsd_dummy_CPPFLAGS =			\
	-I$(top_srcdir)/gnome-settings-daemon		\
	-I$(top_builddir)/gnome-settings-daemon		\
	-I$(top_srcdir)/plugins/common			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	$(AM_CPPFLAGS)

gsd_dummy_LDADD = $(DUMMY_LIBS) $(top_builddir)/gnome-settings-daemon/libgsd.la

EXTRA_DIST = $(NULL)

CLEANFILES = $(NULL)

DISTCLEANFILES = $(NULL)
