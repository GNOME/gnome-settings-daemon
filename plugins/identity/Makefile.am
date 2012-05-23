plugin_name = identity

plugin_LTLIBRARIES = \
	libidentity.la

dbus_built_sources = org.gnome.Identity.c org.gnome.Identity.h

$(dbus_built_sources) : Makefile.am org.gnome.Identity.xml
	gdbus-codegen                                       \
		--interface-prefix org.gnome.Identity.      \
		--c-namespace GsdIdentityService            \
		--c-generate-object-manager                 \
		--generate-c-code org.gnome.Identity        \
		org.gnome.Identity.xml                      \
		$(NULL)

libidentity_headers = \
	gsd-identity-plugin.h            \
	gsd-alarm.h                      \
	gsd-identity.h                   \
	gsd-identity-inquiry.h           \
	gsd-identity-manager.h           \
	gsd-identity-manager-private.h   \
	gsd-identity-service.h           \
	gsd-kerberos-identity.h          \
	gsd-kerberos-identity-inquiry.h  \
	gsd-kerberos-identity-manager.h

gsd-identity-enum-types.h: gsd-identity-enum-types.h.in $(libidentity_headers)
	$(AM_V_GEN) glib-mkenums --template $^ > $@

gsd-identity-enum-types.c: gsd-identity-enum-types.c.in $(libidentity_headers)
	$(AM_V_GEN) glib-mkenums --template $^ > $@

libidentity_la_SOURCES = \
	$(libidentity_headers)           \
	gsd-identity-plugin.c            \
	gsd-alarm.c                      \
	gsd-identity.c                   \
	gsd-identity-enum-types.h        \
	gsd-identity-enum-types.c        \
	gsd-identity-inquiry.c           \
	gsd-identity-manager.c           \
	gsd-identity-service.c           \
	gsd-kerberos-identity.c          \
	gsd-kerberos-identity-inquiry.c  \
	gsd-kerberos-identity-manager.c  \
	$(dbus_built_sources)

BUILT_SOURCES = $(dbus_built_sources) gsd-identity-enum-types.h gsd-identity-enum-types.c

libidentity_la_CPPFLAGS = \
	-I$(top_srcdir)/gnome-settings-daemon \
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\" \
	-DSYSCONFDIR=\""$(sysconfdir)"\" \
	-DLIBDIR=\""$(libdir)"\" \
	-DGSD_SMARTCARD_MANAGER_NSS_DB=\""$(NSS_DATABASE)"\" \
	$(AM_CPPFLAGS)

libidentity_la_CFLAGS = \
	$(PLUGIN_CFLAGS)	\
	$(SETTINGS_PLUGIN_CFLAGS) \
	$(GNOME_CFLAGS)	\
	$(GCR_CFLAGS)	\
	$(KRB5_CFLAGS)	\
	$(AM_CFLAGS)

libidentity_la_LDFLAGS = \
	$(GSD_PLUGIN_LDFLAGS)

libidentity_la_LIBADD = \
	$(SETTINGS_PLUGIN_LIBS)	\
	$(GCR_LIBS)	\
	$(KRB5_LIBS)

noinst_PROGRAMS =				\
	test-identity

test_identity_SOURCES =			\
	gsd-alarm.h                      \
	gsd-alarm.c                      \
	gsd-identity.h                   \
	gsd-identity.c                   \
	gsd-identity-inquiry.c           \
	gsd-identity-inquiry.h           \
	gsd-identity-manager.h           \
	gsd-identity-manager-private.h   \
	gsd-identity-manager.c           \
	gsd-kerberos-identity.c          \
	gsd-kerberos-identity.h          \
	gsd-kerberos-identity-inquiry.c  \
	gsd-kerberos-identity-inquiry.h  \
	gsd-kerberos-identity-manager.h  \
	gsd-kerberos-identity-manager.c  \
	gsd-identity-test.c		 \
	test-identity.c			 \
	$(NULL)

test_identity_CPPFLAGS = \
	-I$(top_srcdir)/data/					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-I$(top_srcdir)/plugins/media-keys/cut-n-paste		\
	-DBINDIR=\"$(bindir)\"					\
	-DPIXMAPDIR=\""$(pkgdatadir)"\"				\
	-DGTKBUILDERDIR=\""$(pkgdatadir)"\"			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	$(AM_CPPFLAGS)

test_identity_CFLAGS = \
	$(PLUGIN_CFLAGS)	\
	$(SETTINGS_PLUGIN_CFLAGS) \
	$(GNOME_CFLAGS)	\
	$(GCR_CFLAGS)	\
	$(KRB5_CFLAGS)	\
	$(AM_CFLAGS)

test_identity_LDFLAGS = \
	$(GSD_PLUGIN_LDFLAGS)

test_identity_LDADD = \
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la			\
	$(top_builddir)/plugins/media-keys/cut-n-paste/libgvc.la	\
	$(SETTINGS_DAEMON_LIBS)			\
	$(SETTINGS_PLUGIN_LIBS)	\
	$(GCR_LIBS)	\
	$(KRB5_LIBS)

@GSD_INTLTOOL_PLUGIN_RULE@

plugin_in_files = \
	identity.gnome-settings-plugin.in

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = \
	$(plugin_in_files)

CLEANFILES = \
	$(plugin_DATA)

DISTCLEANFILES = \
	$(plugin_DATA)
