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

gsd_dummy_LDADD = 					\
	$(top_builddir)/gnome-settings-daemon/libgsd.la	\
	$(top_builddir)/plugins/common/libcommon.la	\
	$(DUMMY_LIBS)

unitdir = $(systemduserunitdir)

unit_DATA =
if !SMARTCARD_SUPPORT
unit_DATA += gsd-smartcard.service
endif
if !HAVE_WACOM
unit_DATA += gsd-wacom.service
endif
if !BUILD_PRINT_NOTIFICATIONS
unit_DATA += gsd-print-notifications.service
endif
if !BUILD_RFKILL
unit_DATA += gsd-rfkill.service
endif

%.service: gsd-dummy.service.in Makefile
	$(AM_V_GEN) PLUGINNAME=`echo $@ | sed 's,.service,,'` && sed -e "s|\@pluginname\@|$$PLUGINNAME|" -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST = gsd-dummy.service.in

CLEANFILES = $(NULL)

DISTCLEANFILES = $(NULL)
