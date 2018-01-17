NULL =

plugin_name = clipboard

libexec_PROGRAMS = gsd-clipboard
gsd_clipboard_SOURCES =	\
	main.c			\
	gsd-clipboard-manager.h	\
	gsd-clipboard-manager.c	\
	xutils.h		\
	xutils.c		\
	list.h			\
	list.c			\
	$(NULL)

gsd_clipboard_CFLAGS =		\
	$(CLIPBOARD_CFLAGS)	\
	$(AM_CFLAGS)

gsd_clipboard_CPPFLAGS =					\
	$(PLUGIN_CFLAGS)					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"

gsd_clipboard_LDADD = 						\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(CLIPBOARD_LIBS)

unitdir = $(systemduserunitdir)
unit_in_files = gsd-clipboard.service.in
unit_DATA = gsd-clipboard.service

gsd-clipboard.service: $(unit_in_files) Makefile
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
