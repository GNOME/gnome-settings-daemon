plugin_name = common

noinst_LTLIBRARIES = libcommon.la

GSD_COMMON_ENUM_FILES = gsd-common-enums.c gsd-common-enums.h
BUILT_SOURCES = $(GSD_COMMON_ENUM_FILES)

gsd-common-enums.h: gsd-device-manager.h Makefile
	$(AM_V_GEN)($(GLIB_MKENUMS) \
			--fhead "#ifndef GSD_COMMON_ENUMS_H\n#define GSD_COMMON_ENUMS_H\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
			--fprod "/* enumerations from \"@filename@\" */\n" \
			--vhead "GType @enum_name@_get_type (void) G_GNUC_CONST;\n#define GSD_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n" \
			--ftail "G_END_DECLS\n\n#endif /* !GSD_COMMON_ENUMS_H */" \
		$(srcdir)/gsd-device-manager.h > $@)

gsd-common-enums.c: gsd-device-manager.h Makefile gsd-common-enums.h
	$(AM_V_GEN)($(GLIB_MKENUMS) \
			--fhead "#include \"gsd-device-manager.h\"\n#include \"gsd-common-enums.h\"\n" \
			--fprod "\n/* enumerations from \"@filename@\" */" \
			--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {" \
			--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
			--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
		$(srcdir)/gsd-device-manager.h > $@)

libcommon_la_SOURCES = \
	gsd-common-enums.c	\
	gsd-common-enums.h	\
	gsd-device-manager.c	\
	gsd-device-manager.h	\
	gsd-device-manager-x11.c  \
	gsd-device-manager-x11.h  \
	gsd-device-mapper.c	\
	gsd-device-mapper.h	\
	gsd-input-helper.c	\
	gsd-input-helper.h	\
	gsd-resources.c		\
	gsd-resources.h		\
	gsd-settings-migrate.c	\
	gsd-settings-migrate.h	\
	gsd-shell-helper.c	\
	gsd-shell-helper.h

if HAVE_GUDEV
libcommon_la_SOURCES += \
	gsd-device-manager-udev.c \
	gsd-device-manager-udev.h
endif

libcommon_la_CPPFLAGS = \
	-I$(top_srcdir)/gnome-settings-daemon	\
	-I$(top_builddir)/gnome-settings-daemon	\
	-I$(top_srcdir)/data/

libcommon_la_CFLAGS = $(COMMON_CFLAGS)

libcommon_la_LDFLAGS = \
	$(GSD_PLUGIN_LDFLAGS)

libcommon_la_LIBADD  = \
	$(top_builddir)/gnome-settings-daemon/libgsd.la \
	$(COMMON_LIBS)

libexec_PROGRAMS = gsd-test-input-helper

gsd_test_input_helper_SOURCES = test-input-helper.c
gsd_test_input_helper_LDADD = libcommon.la
gsd_test_input_helper_CFLAGS = $(libcommon_la_CFLAGS)

EXTRA_DIST = $(scripts_DATA) daemon-skeleton.h daemon-skeleton-gtk.h

CLEANFILES = \
	$(GSD_COMMON_ENUM_FILES)

DISTCLEANFILES =

glib_resources_c = gsd-resources.c
glib_resources_h = gsd-resources.h
glib_resources_xml = gsd.gresources.xml
glib_resources_namespace = gsd
include Makefile.am.gresources
