plugin_name = wacom

org.gnome.settings-daemon.plugins.wacom.policy.in: org.gnome.settings-daemon.plugins.wacom.policy.in.in Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

gsd-wacom-resources.c: wacom.gresource.xml tablet-layout.css
	glib-compile-resources 				\
		--target=$@ 				\
		--sourcedir=$(srcdir) 			\
		--generate-source 			\
		--c-name gsd_wacom 			\
		$(srcdir)/wacom.gresource.xml

@INTLTOOL_POLICY_RULE@
polkit_policydir = $(datadir)/polkit-1/actions
polkit_policy_in_files = org.gnome.settings-daemon.plugins.wacom.policy.in
polkit_policy_DATA = $(polkit_policy_in_files:.policy.in=.policy)

# so it always gets included in the tarball
gsd_wacom_led_helper_SOURCES =	gsd-wacom-led-helper.c
gsd_wacom_oled_helper_SOURCES =	gsd-wacom-oled-helper.c gsd-wacom-oled-constants.h

EXTRA_DIST = $(gsd_wacom_led_helper_SOURCES) wacom.gresource.xml tablet-layout.css \
	     $(gsd_wacom_oled_helper_SOURCES)

if HAVE_GUDEV
libexec_PROGRAMS = gsd-wacom-led-helper gsd-wacom-oled-helper

gsd_wacom_led_helper_LDFLAGS =				\
	$(BACKLIGHT_HELPER_LIBS)			\
	-lm

gsd_wacom_oled_helper_LDFLAGS =				\
	$(WACOM_OLED_LIBS)				\
	-lm

gsd_wacom_led_helper_CFLAGS =				\
	$(BACKLIGHT_HELPER_CFLAGS)

gsd_wacom_oled_helper_CFLAGS =				\
	$(WACOM_OLED_CFLAGS)
else
libexec_PROGRAMS =
endif

EXTRA_DIST += org.gnome.settings-daemon.plugins.wacom.policy.in.in

libexec_PROGRAMS += gsd-wacom gsd-list-wacom gsd-test-wacom-osd

gsd_wacom_SOURCES =		\
	main.c			\
	gsd-wacom-manager.c	\
	gsd-wacom-manager.h	\
	gsd-wacom-key-shortcut-button.h	\
	gsd-wacom-key-shortcut-button.c	\
	gsd-wacom-button-editor.h	\
	gsd-wacom-button-editor.c	\
	gsd-wacom-osd-window.h	\
	gsd-wacom-osd-window.c	\
	gsd-wacom-oled.h	\
	gsd-wacom-oled.c	\
	gsd-wacom-device.c	\
	gsd-wacom-device.h	\
	gsd-wacom-resources.c

gsd_wacom_CPPFLAGS = \
	-I$(top_srcdir)/data/					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon		\
	-I$(top_srcdir)/plugins/common				\
	-DBINDIR=\"$(bindir)\"					\
	-DPIXMAPDIR=\""$(pkgdatadir)"\"				\
	-DGTKBUILDERDIR=\""$(pkgdatadir)"\"			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	-DLIBEXECDIR=\""$(libexecdir)"\"

gsd_wacom_CFLAGS =			\
	$(PLUGIN_CFLAGS)		\
	$(WACOM_CFLAGS)

gsd_wacom_LDADD =						\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(WACOM_LIBS)						\
	-lm

gsd_list_wacom_SOURCES =	\
	list-wacom.c		\
	gsd-wacom-device.c	\
	gsd-wacom-device.h

gsd_list_wacom_CPPFLAGS =					\
	-I$(top_srcdir)/data/					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-DBINDIR=\"$(bindir)\"					\
	-DPIXMAPDIR=\""$(pkgdatadir)"\"				\
	-DGTKBUILDERDIR=\""$(pkgdatadir)"\"			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"

gsd_list_wacom_CFLAGS = $(WACOM_CFLAGS)

gsd_list_wacom_LDADD =						\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(WACOM_LIBS)						\
	-lm

gsd_test_wacom_osd_SOURCES =					\
	test-osd-window.c					\
	gsd-wacom-key-shortcut-button.h				\
	gsd-wacom-key-shortcut-button.c				\
	gsd-wacom-button-editor.h				\
	gsd-wacom-button-editor.c				\
	gsd-wacom-osd-window.h					\
	gsd-wacom-osd-window.c					\
	gsd-wacom-device.c					\
	gsd-wacom-device.h					\
	gsd-wacom-resources.c

gsd_test_wacom_osd_CPPFLAGS = \
	-I$(top_srcdir)/data/					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-DBINDIR=\"$(bindir)\"					\
	-DPIXMAPDIR=\""$(pkgdatadir)"\"				\
	-DGTKBUILDERDIR=\""$(pkgdatadir)"\"			\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	-DLIBEXECDIR=\""$(libexecdir)"\"

gsd_test_wacom_osd_CFLAGS =					\
	$(WACOM_CFLAGS)

gsd_test_wacom_osd_LDADD = \
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(WACOM_LIBS)						\
	-lm

desktopdir = $(sysconfdir)/xdg/autostart
desktop_in_files = org.gnome.SettingsDaemon.Wacom.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

org.gnome.SettingsDaemon.Wacom.desktop: $(desktop_in_files) Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST += $(desktop_in_files) README.config-storage
CLEANFILES =						\
	$(desktop_DATA)					\
	gsd-wacom-resources.c				\
	org.gnome.settings-daemon.plugins.wacom.policy	\
	org.gnome.settings-daemon.plugins.wacom.policy.in
