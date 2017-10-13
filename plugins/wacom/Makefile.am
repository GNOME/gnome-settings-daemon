plugin_name = wacom

org.gnome.settings-daemon.plugins.wacom.policy.in: org.gnome.settings-daemon.plugins.wacom.policy.in.in Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

@INTLTOOL_POLICY_RULE@
polkit_policydir = $(datadir)/polkit-1/actions
polkit_policy_in_files = org.gnome.settings-daemon.plugins.wacom.policy.in
polkit_policy_DATA = $(polkit_policy_in_files:.policy.in=.policy)

libexec_PROGRAMS = gsd-wacom

if HAVE_GUDEV
libexec_PROGRAMS += gsd-wacom-led-helper gsd-wacom-oled-helper

gsd_wacom_led_helper_LDFLAGS =				\
	$(BACKLIGHT_HELPER_LIBS)			\
	$(LIBM)

gsd_wacom_oled_helper_LDFLAGS =				\
	$(WACOM_OLED_LIBS)				\
	$(LIBM)

gsd_wacom_led_helper_CFLAGS =				\
	$(BACKLIGHT_HELPER_CFLAGS)

gsd_wacom_oled_helper_CFLAGS =				\
	$(WACOM_OLED_CFLAGS)
endif

EXTRA_DIST = org.gnome.settings-daemon.plugins.wacom.policy.in.in

gsd_wacom_SOURCES =            \
	main.c                  \
	gsd-wacom-manager.c     \
	gsd-wacom-manager.h     \
	gsd-wacom-oled-constants.h \
	gsd-wacom-oled.h        \
	gsd-wacom-oled.c

gsd_wacom_CPPFLAGS = \
	-I$(top_srcdir)/data/                                   \
	-I$(top_srcdir)/gnome-settings-daemon                   \
	-I$(top_builddir)/gnome-settings-daemon         \
	-I$(top_srcdir)/plugins/common                          \
	-DBINDIR=\"$(bindir)\"                                  \
	-DPIXMAPDIR=\""$(pkgdatadir)"\"                         \
	-DGTKBUILDERDIR=\""$(pkgdatadir)"\"                     \
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"      \
	-DLIBEXECDIR=\""$(libexecdir)"\"

gsd_wacom_CFLAGS =                     \
	$(PLUGIN_CFLAGS)                \
	$(WACOM_CFLAGS)

gsd_wacom_LDADD =                                              \
	$(top_builddir)/gnome-settings-daemon/libgsd.la         \
	$(top_builddir)/plugins/common/libcommon.la             \
	$(WACOM_LIBS)                                           \
	$(LIBM)

desktopdir = $(sysconfdir)/xdg/autostart
desktop_in_files = org.gnome.SettingsDaemon.Wacom.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

org.gnome.SettingsDaemon.Wacom.desktop: $(desktop_in_files) Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST += $(desktop_in_files)
CLEANFILES =						\
	$(desktop_DATA)					\
	org.gnome.settings-daemon.plugins.wacom.policy	\
	org.gnome.settings-daemon.plugins.wacom.policy.in
