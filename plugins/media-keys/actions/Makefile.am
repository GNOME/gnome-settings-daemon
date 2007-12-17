NULL =

INCLUDES = 				\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(GST_CFLAGS)			\
	$(NULL)

noinst_LTLIBRARIES = libacme.la

libacme_la_SOURCES = 		\
	acme.h			\
	acme-volume.c 		\
	acme-volume.h 		\
	acme-volume-dummy.c 	\
	acme-volume-dummy.h	\
	$(NULL)

libacme_la_LIBADD =		\
	$(NULL)

GST_SOURCES = acme-volume-gstreamer.c acme-volume-gstreamer.h
ALSA_SOURCES = acme-volume-alsa.c acme-volume-alsa.h
OSS_SOURCES = acme-volume-oss.c acme-volume-oss.h

if HAVE_GSTREAMER
INCLUDES += -DHAVE_GSTREAMER
libacme_la_SOURCES += $(GST_SOURCES)
libacme_la_LIBADD += $(GST_LIBS)
else # HAVE_GSTREAMER
if HAVE_ALSA
INCLUDES += -DHAVE_ALSA
libacme_la_SOURCES += $(ALSA_SOURCES)
libacme_la_LIBADD += $(ALSA_LIBS)
else # HAVE_ALSA
if HAVE_OSS
INCLUDES += -DHAVE_OSS
libacme_la_SOURCES += $(OSS_SOURCES)
endif # HAVE_OSS
endif # HAVE_ALSA
endif # HAVE_GSTREAMER

EXTRA_libacme_la_SOURCES = 	\
	$(GST_SOURCES)		\
	$(ALSA_SOURCES)		\
	$(OSS_SOURCES)		\
	$(NULL)

plugindir = $(libdir)/gnome-settings-daemon/plugins/media-keys

gladedir = $(plugindir)
glade_DATA = 		\
	acme.glade	\
	$(NULL)

pixmapsdir = $(plugindir)
pixmaps_DATA = 		\
	acme-eject.png	\
	$(NULL)

EXTRA_DIST =			\
	acme.h			\
	$(glade_DATA)		\
	$(pixmaps_DATA)		\
	$(NULL)
