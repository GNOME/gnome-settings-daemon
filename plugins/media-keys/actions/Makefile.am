NULL =

noinst_LTLIBRARIES = libacme.la

libacme_la_SOURCES = 		\
	acme.h			\
	acme-volume.c 		\
	acme-volume.h 		\
	acme-volume-dummy.c 	\
	acme-volume-dummy.h	\
	$(NULL)

libacme_la_CPPFLAGS = \
	-I$(top_srcdir)/src		\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	$(AM_CPPFLAGS)

libacme_la_CFLAGS = \
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(GST_CFLAGS)			\
	$(AM_CFLAGS)

libacme_la_LIBADD =		\
	$(NULL)

if HAVE_GSTREAMER
libacme_la_SOURCES += \
	acme-volume-gstreamer.c \
	acme-volume-gstreamer.h \
	$(NULL)

libacme_la_CPPFLAGS += -DHAVE_GSTREAMER

libacme_la_LIBADD += $(GST_LIBS)

else # HAVE_GSTREAMER

if HAVE_ALSA
libacme_la_SOURCES += \
	acme-volume-alsa.c \
	acme-volume-alsa.h \
	$(NULL)

libacme_la_CPPFLAGS += -DHAVE_ALSA

libacme_la_LIBADD += $(ALSA_LIBS)

else # HAVE_ALSA

if HAVE_OSS
libacme_la_SOURCES += \
	acme-volume-oss.c \
	acme-volume-oss.h \
	$(NULL)

libacme_la_CPPFLAGS += -DHAVE_OSS

libacme_la_SOURCES += $(OSS_SOURCES)

endif # HAVE_OSS
endif # HAVE_ALSA
endif # HAVE_GSTREAMER

gladedir = $(pkgdatadir)
glade_DATA = 		\
	acme.glade	\
	$(NULL)

pixmapsdir = $(pkgdatadir)
pixmaps_DATA = 		\
	acme-eject.png	\
	$(NULL)

EXTRA_DIST =			\
	acme.h			\
	$(glade_DATA)		\
	$(pixmaps_DATA)		\
	$(NULL)
