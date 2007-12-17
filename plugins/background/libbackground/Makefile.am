EXTRA_DIST = ChangeLog

INCLUDES = \
	-DGNOMELOCALEDIR="\"$(datadir)/locale\""			\
	-I$(top_srcdir)/						\
	$(SETTINGS_PLUGIN_CFLAGS)

noinst_LTLIBRARIES = libbackground.la

libbackground_la_SOURCES = \
	applier.c	applier.h	\
	preferences.c	preferences.h
