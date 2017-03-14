AC_PREREQ([2.60])

AC_INIT([gnome-settings-daemon],
        [3.22.2],
        [http://bugzilla.gnome.org/enter_bug.cgi?product=gnome-settings-daemon])

AC_CONFIG_SRCDIR([gnome-settings-daemon/gnome-settings-manager.c])

AM_INIT_AUTOMAKE([1.9 tar-ustar dist-xz no-dist-gzip check-news])
AM_MAINTAINER_MODE([enable])

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

m4_define([gsd_api_version_major],[3])
m4_define([gsd_api_version_minor],[0])
m4_define([gsd_api_version],[gsd_api_version_major.gsd_api_version_minor])
GSD_API_VERSION="gsd_api_version"
AC_SUBST(GSD_API_VERSION)

AC_STDC_HEADERS
AC_PROG_CXX
AM_PROG_CC_C_O
AC_PROG_LIBTOOL

AC_HEADER_STDC

AC_SUBST(VERSION)

AC_CONFIG_HEADERS([config.h])

IT_PROG_INTLTOOL([0.37.1])

GETTEXT_PACKAGE=gnome-settings-daemon
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Name of default gettext domain])

AM_GLIB_GNU_GETTEXT

GSD_INTLTOOL_PLUGIN_RULE='%.gnome-settings-plugin:   %.gnome-settings-plugin.in   $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*.po) ; LC_ALL=C $(INTLTOOL_MERGE) -d -u -c $(top_builddir)/po/.intltool-merge-cache $(top_srcdir)/po $< [$]@'
AC_SUBST([GSD_INTLTOOL_PLUGIN_RULE])

dnl ---------------------------------------------------------------------------
dnl - Dependencies
dnl ---------------------------------------------------------------------------

GLIB_REQUIRED_VERSION=2.37.7
GIO_REQUIRED_VERSION=${GLIB_REQUIRED_VERSION}
GTK_REQUIRED_VERSION=3.15.3
GCONF_REQUIRED_VERSION=2.6.1
GNOME_DESKTOP_REQUIRED_VERSION=3.11.1
LIBGWEATHER_REQUIRED_VERSION=3.9.5
LIBNOTIFY_REQUIRED_VERSION=0.7.3
PA_REQUIRED_VERSION=2.0
LIBWACOM_REQUIRED_VERSION=0.7
LIBRSVG_REQUIRED_VERSION=2.36.2
PANGO_REQUIRED_VERSION=1.20.0
POLKIT_REQUIRED_VERSION=0.103
UPOWER_REQUIRED_VERSION=0.99.0
GSETTINGS_DESKTOP_SCHEMAS_REQUIRED_VERSION=3.20.0
GEOCODE_GLIB_REQUIRED_VERSION=3.10.0
GEOCLUE_REQUIRED_VERSION=2.3.1
NM_REQUIRED_VERSION=1.0
LCMS_REQUIRED_VERSION=2.2

EXTRA_COMPILE_WARNINGS(yes)

PKG_CHECK_MODULES(SETTINGS_DAEMON,
        gtk+-3.0 >= $GTK_REQUIRED_VERSION
        gio-2.0 >= $GIO_REQUIRED_VERSION
        gmodule-2.0
        gthread-2.0
	gsettings-desktop-schemas >= $GSETTINGS_DESKTOP_SCHEMAS_REQUIRED_VERSION
)

PKG_CHECK_MODULES(SETTINGS_PLUGIN,
        gtk+-3.0 >= $GTK_REQUIRED_VERSION
        gio-2.0 >= $GIO_REQUIRED_VERSION
        libnotify >= $LIBNOTIFY_REQUIRED_VERSION
	gsettings-desktop-schemas >= $GSETTINGS_DESKTOP_SCHEMAS_REQUIRED_VERSION
        x11
)

GSD_PLUGIN_LDFLAGS="-export_dynamic -module -avoid-version -no-undefined"
case $host_os in
  darwin*)
    GSD_PLUGIN_LDFLAGS="${GSD_PLUGIN_LDFLAGS} -Wl,-bundle_loader,\$(top_builddir)/gnome-settings-daemon/gnome-settings-daemon"
    ;;
esac
AC_SUBST([GSD_PLUGIN_LDFLAGS])

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)
AC_PATH_PROG([GLIB_MKENUMS],[glib-mkenums])

dnl ================================================================
dnl GSettings stuff
dnl ================================================================

GLIB_GSETTINGS

dnl ---------------------------------------------------------------------------
dnl - Check for gnome-desktop
dnl ---------------------------------------------------------------------------
PKG_CHECK_MODULES(GNOME_DESKTOP, gnome-desktop-3.0 >= $GNOME_DESKTOP_REQUIRED_VERSION)

dnl ---------------------------------------------------------------------------
dnl - Check for libnotify
dnl ---------------------------------------------------------------------------

PKG_CHECK_MODULES(LIBNOTIFY, libnotify >= $LIBNOTIFY_REQUIRED_VERSION,
		  [have_libnotify=yes], have_libnotify=no)
if test "x$have_libnotify" = xno ; then
	AC_MSG_ERROR([libnotify is required to build gnome-settings-daemon])
fi
AC_SUBST(LIBNOTIFY_CFLAGS)
AC_SUBST(LIBNOTIFY_LIBS)

dnl ---------------------------------------------------------------------------
dnl - GUdev integration (default enabled)
dnl ---------------------------------------------------------------------------
GUDEV_PKG=""
AC_ARG_ENABLE(gudev, AS_HELP_STRING([--disable-gudev],[Disable GUdev support (not optional on Linux platforms)]), enable_gudev=$enableval)
if test x$enable_gudev != xno; then
	PKG_CHECK_MODULES(GUDEV, gudev-1.0, have_gudev="yes", have_gudev="no")
	if test "x$have_gudev" = "xyes"; then
		AC_DEFINE(HAVE_GUDEV, 1, [define if GUdev is available])
		GUDEV_PKG="gudev-1.0"
	else
		if test x$enable_gudev = xyes; then
			AC_MSG_ERROR([GUdev enabled but not found])
		fi
	fi
else
	have_gudev=no
fi
AM_CONDITIONAL(HAVE_GUDEV, test x$have_gudev = xyes)

dnl ---------------------------------------------------------------------------
dnl - ALSA integration (default enabled)
dnl ---------------------------------------------------------------------------
ALSA_PKG=""
AC_ARG_ENABLE(alsa, AS_HELP_STRING([--disable-alsa],[Disable ALSA support (not optional on Linux platforms)]), enable_alsa=$enableval)
if test x$enable_alsa != xno; then
	PKG_CHECK_MODULES(ALSA, alsa, have_alsa="yes", have_alsa="no")
	if test "x$have_alsa" = "xyes"; then
		AC_DEFINE(HAVE_ALSA, 1, [define if ALSA is available])
		ALSA_PKG="alsa"
	else
		if test x$enable_alsa = xyes; then
			AC_MSG_ERROR([ALSA enabled but not found])
		fi
	fi
else
	have_alsa=no
fi
AM_CONDITIONAL(HAVE_ALSA, test x$have_alsa = xyes)

dnl ---------------------------------------------------------------------------
dnl - Check for libwayland-client
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(wayland,
        AC_HELP_STRING([--disable-wayland], [Disable wayland support (default: auto)]),
        enable_wayland=$enableval, enable_wayland=auto)
if test x$enable_wayland != xno; then
        PKG_CHECK_MODULES(WAYLAND, wayland-client, have_wayland=yes, have_wayland=no)
        if test x$have_wayland = xyes; then
                AC_DEFINE(HAVE_WAYLAND, 1, [Define if libwayland-client is available])
        else
                if test x$enable_wayland = xyes; then
                        AC_MSG_ERROR([Wayland enabled but not found])
                fi
        fi
fi

dnl ---------------------------------------------------------------------------
dnl - common
dnl ---------------------------------------------------------------------------

PKG_CHECK_MODULES(COMMON, x11 kbproto xi $GUDEV_PKG)

dnl ---------------------------------------------------------------------------
dnl - XTest
dnl ---------------------------------------------------------------------------

PKG_CHECK_MODULES(XTEST, x11 xtst)
AC_SUBST(XTEST_CFLAGS)
AC_SUBST(XTEST_LIBS)

dnl ---------------------------------------------------------------------------
dnl - mouse
dnl ---------------------------------------------------------------------------

PKG_CHECK_MODULES(MOUSE, x11 xi)

dnl ---------------------------------------------------------------------------
dnl - xsettings
dnl ---------------------------------------------------------------------------

PKG_CHECK_MODULES(XSETTINGS, fontconfig gnome-desktop-3.0 >= $GNOME_DESKTOP_REQUIRED_VERSION)

dnl ---------------------------------------------------------------------------
dnl - Housekeeping plugin stuff
dnl ---------------------------------------------------------------------------

PKG_CHECK_MODULES(GIOUNIX, [gio-unix-2.0])

dnl ---------------------------------------------------------------------------
dnl - media-keys plugin stuff
dnl ---------------------------------------------------------------------------

PKG_CHECK_MODULES(MEDIA_KEYS, [gio-unix-2.0 libpulse >= $PA_REQUIRED_VERSION $GUDEV_PKG libpulse-mainloop-glib >= $PA_REQUIRED_VERSION libcanberra-gtk3 upower-glib >= $UPOWER_REQUIRED_VERSION])
PKG_CHECK_MODULES(GVC, [gobject-2.0 libpulse >= $PA_REQUIRED_VERSION libpulse-mainloop-glib >= $PA_REQUIRED_VERSION $ALSA_PKG])
AM_CONDITIONAL(HAVE_INTROSPECTION, false)

dnl ---------------------------------------------------------------------------
dnl - xrandr plugin stuff
dnl ---------------------------------------------------------------------------

PKG_CHECK_MODULES(XRANDR, [gnome-desktop-3.0 >= $GNOME_DESKTOP_REQUIRED_VERSION upower-glib >= $UPOWER_REQUIRED_VERSION])

dnl ---------------------------------------------------------------------------
dnl - orientation plugin stuff
dnl ---------------------------------------------------------------------------

PKG_CHECK_MODULES(ORIENTATION, [gnome-desktop-3.0 >= $GNOME_DESKTOP_REQUIRED_VERSION])

dnl ---------------------------------------------------------------------------
dnl - sound plugin stuff
dnl ---------------------------------------------------------------------------

PKG_CHECK_MODULES(SOUND, [libpulse >= $PA_REQUIRED_VERSION $GUDEV_PKG libpulse-mainloop-glib >= $PA_REQUIRED_VERSION])

# ---------------------------------------------------------------------------
# Power
# ---------------------------------------------------------------------------
PKG_CHECK_MODULES(POWER, upower-glib >= $UPOWER_REQUIRED_VERSION gnome-desktop-3.0 >= $GNOME_DESKTOP_REQUIRED_VERSION $GUDEV_PKG libcanberra-gtk3 libnotify x11 xext xtst)

if test x$have_gudev != xno; then
	PKG_CHECK_MODULES(BACKLIGHT_HELPER,
		glib-2.0 >= $GLIB_REQUIRED_VERSION
		gudev-1.0
	)
fi

dnl ---------------------------------------------------------------------------
dnl - color
dnl ---------------------------------------------------------------------------

PKG_CHECK_MODULES(COLOR, [colord >= 1.0.2 gnome-desktop-3.0 >= $GNOME_DESKTOP_REQUIRED_VERSION libcanberra-gtk3 lcms2 >= $LCMS_REQUIRED_VERSION])

dnl ---------------------------------------------------------------------------
dnl - datetime
dnl ---------------------------------------------------------------------------

AC_CHECK_LIBM
AC_SUBST(LIBM)

PKG_CHECK_MODULES(DATETIME,
        libgeoclue-2.0 >= $GEOCLUE_REQUIRED_VERSION
	geocode-glib-1.0 >= $GEOCODE_GLIB_REQUIRED_VERSION
	gweather-3.0 >= $LIBGWEATHER_REQUIRED_VERSION
	polkit-gobject-1 >= $POLKIT_REQUIRED_VERSION
)

dnl ---------------------------------------------------------------------------
dnl - wacom (disabled for s390/s390x and non Linux platforms)
dnl ---------------------------------------------------------------------------

case $host_os in
  linux*)
    if test "$host_cpu" = s390 -o "$host_cpu" = s390x; then
      have_wacom=no
    else
      if test x$enable_gudev != xno; then
        PKG_CHECK_MODULES(LIBWACOM, [libwacom >= $LIBWACOM_REQUIRED_VERSION])
        PKG_CHECK_MODULES(WACOM, [libwacom >= $LIBWACOM_REQUIRED_VERSION x11 xi xtst gudev-1.0 gnome-desktop-3.0 >= $GNOME_DESKTOP_REQUIRED_VERSION xorg-wacom librsvg-2.0 >= $LIBRSVG_REQUIRED_VERSION libnotify >= $LIBNOTIFY_REQUIRED_VERSION pango >= $PANGO_REQUIRED_VERSION])
        PKG_CHECK_MODULES(WACOM_OLED, [gudev-1.0])
      else
        AC_MSG_ERROR([GUdev is necessary to compile Wacom support])
      fi
      AC_DEFINE_UNQUOTED(HAVE_WACOM, 1, [Define to 1 if wacom support is available])
      have_wacom=yes
    fi
    ;;
  *)
    have_wacom=no
    ;;
esac
AM_CONDITIONAL(HAVE_WACOM, test x$have_wacom = xyes)

dnl ==============================================
dnl smartcard section
dnl ==============================================
have_smartcard_support=false
AC_ARG_ENABLE(smartcard-support,
  AC_HELP_STRING([--disable-smartcard-support],
                 [turn off smartcard support]),
       [case "${enableval}" in
               yes) WANT_SMARTCARD_SUPPORT=yes ;;
               no)  WANT_SMARTCARD_SUPPORT=no ;;
               *) AC_MSG_ERROR(bad value ${enableval} for --disable-smartcard-support) ;;
       esac],
       [WANT_SMARTCARD_SUPPORT=yes])

if test x$WANT_SMARTCARD_SUPPORT = xyes ; then
       NSS_REQUIRED_VERSION=3.11.2
       PKG_CHECK_MODULES(NSS, nss >= $NSS_REQUIRED_VERSION,
             [have_smartcard_support=true
              AC_DEFINE(SMARTCARD_SUPPORT, 1, [Define if smartcard support should be enabled])],
             [have_smartcard_support=false])
fi
AM_CONDITIONAL(SMARTCARD_SUPPORT, test "x$have_smartcard_support" = "xtrue")

AC_SUBST(NSS_CFLAGS)
AC_SUBST(NSS_LIBS)

AC_ARG_WITH(nssdb,
  AC_HELP_STRING([--with-nssdb],
                 [where system NSS database is]))

NSS_DATABASE=""
if test "x$have_smartcard_support" = "xtrue"; then
        if ! test -z "$with_nssdb" ; then
                NSS_DATABASE="$with_nssdb"
        else
                NSS_DATABASE="${sysconfdir}/pki/nssdb"
        fi
else
        if ! test -z "$with_nssdb" ; then
                AC_MSG_WARN([nssdb specified when smartcard support is disabled])
        fi
fi

AC_SUBST(NSS_DATABASE)


# ---------------------------------------------------------------------------
# CUPS
# ---------------------------------------------------------------------------

AC_ARG_ENABLE(cups,
              AS_HELP_STRING([--disable-cups], [disable CUPS support (default: enabled)]),,
              enable_cups=yes)

if test x"$enable_cups" != x"no" ; then
   AC_PROG_SED

   AC_PATH_PROG(CUPS_CONFIG, cups-config)

   if test x$CUPS_CONFIG = x; then
     AC_MSG_ERROR([cups-config not found but CUPS support requested])
   fi

  CUPS_API_VERSION=`$CUPS_CONFIG --api-version`
  CUPS_API_MAJOR=`echo $ECHO_N $CUPS_API_VERSION | cut -d . -f 1`
  CUPS_API_MINOR=`echo $ECHO_N $CUPS_API_VERSION | cut -d . -f 2`

  AC_CHECK_HEADERS([cups/cups.h cups/http.h cups/ipp.h],,
                   AC_MSG_ERROR([CUPS headers not found but CUPS support requested]))

   if ! test $CUPS_API_MAJOR -gt 1 -o \
             $CUPS_API_MAJOR -eq 1 -a $CUPS_API_MINOR -ge 4 ; then
      AC_MSG_ERROR([CUPS 1.4 or newer not found, but CUPS support requested])
   fi

   # https://bugzilla.gnome.org/show_bug.cgi?id=696571
   CUPS_CPPFLAGS=""
   if test $CUPS_API_MAJOR -gt 1 -o \
           $CUPS_API_MAJOR -eq 1 -a $CUPS_API_MINOR -ge 6 ; then
      CUPS_CPPFLAGS=-D_PPD_DEPRECATED=""
   fi

   CUPS_CFLAGS=`$CUPS_CONFIG --cflags | $SED -e 's/-O\w*//g' -e 's/-m\w*//g'`
   CUPS_LIBS=`$CUPS_CONFIG --libs`
   AC_SUBST(CUPS_CPPFLAGS)
   AC_SUBST(CUPS_CFLAGS)
   AC_SUBST(CUPS_LIBS)
fi

AM_CONDITIONAL(BUILD_PRINT_NOTIFICATIONS, [test x"$enable_cups" = x"yes"])

# ---------------------------------------------------------------------------
# Rfkill
# ---------------------------------------------------------------------------

AC_ARG_ENABLE(rfkill,
              AS_HELP_STRING([--disable-rfkill], [disable rfkill support (default: enabled)]),,
              enable_rfkill=yes, enabled_rfkill=no)

if test x"$enable_rfkill" != x"no" ; then
  AC_CHECK_HEADERS([linux/rfkill.h],,
                   AC_MSG_ERROR([RFKill headers not found but rfkill support requested]))
fi

AM_CONDITIONAL(BUILD_RFKILL, [test x"$enable_rfkill" = x"yes"])

dnl ---------------------------------------------------------------------------
dnl Sharing plugin
dnl ---------------------------------------------------------------------------

AC_ARG_ENABLE(network-manager,
        AS_HELP_STRING([--disable-network-manager],
                       [Disable NetworkManager support]),
        enable_network_manager=$enableval,
        enable_network_manager=yes)

if test "x$enable_network_manager" = "xyes" ; then
        NM_MODULE="libnm >= $NM_REQUIRED_VERSION"
        AC_DEFINE(HAVE_NETWORK_MANAGER, 1, [Defined if NetworkManager support is enabled])
else
        NM_MODULE=
fi

PKG_CHECK_MODULES(SHARING, gio-2.0 $NM_MODULE)

# ---------------------------------------------------------------------------
# Enable Profiling
# ---------------------------------------------------------------------------
AC_ARG_ENABLE(profiling,
	[AC_HELP_STRING([--enable-profiling],
	[turn on profiling])],
	, enable_profiling=no)
if test "x$enable_profiling" = "xyes"; then
    AC_DEFINE(ENABLE_PROFILING,1,[enable profiling])
fi

# ---------------------------------------------------------------------------
# Plugins
# ---------------------------------------------------------------------------

plugindir='$(libdir)/gnome-settings-daemon-gsd_api_version'
AC_SUBST([plugindir])

PLUGIN_CFLAGS="-DG_LOG_DOMAIN=\"\\\"\$(plugin_name)-plugin\\\"\" -DPLUGIN_NAME=\"\\\"\$(plugin_name)\\\"\" "
AC_SUBST(PLUGIN_CFLAGS)

AC_ARG_ENABLE(man,
              [AS_HELP_STRING([--enable-man],
                              [generate man pages [default=yes]])],,
              enable_man=yes)
if test "$enable_man" != no; then
  AC_PATH_PROG([XSLTPROC], [xsltproc])
  if test -z "$XSLTPROC"; then
    AC_MSG_ERROR([xsltproc is required for --enable-man])
  fi
fi
AM_CONDITIONAL(ENABLE_MAN, test "$enable_man" != no)

dnl ---------------------------------------------------------------------------
dnl - Finish
dnl ---------------------------------------------------------------------------


# Turn on the additional warnings last, so warnings don't affect other tests.

AC_ARG_ENABLE(more-warnings,
	[AC_HELP_STRING([--enable-more-warnings],
	[Maximum compiler warnings])],
	set_more_warnings="$enableval",[
		if test -d $srcdir/.git; then
			set_more_warnings=yes
		else
			set_more_warnings=no
		fi
        ])
AC_MSG_CHECKING(for more warnings)
if test "$GCC" = "yes" -a "$set_more_warnings" != "no"; then
        AC_MSG_RESULT(yes)
        CFLAGS="\
        -Wall \
        -Wchar-subscripts -Wmissing-declarations -Wmissing-prototypes \
        -Wnested-externs -Wpointer-arith \
        -Wcast-align -Wsign-compare \
        $CFLAGS"

        for option in -Wno-strict-aliasing -Wno-sign-compare; do
                SAVE_CFLAGS="$CFLAGS"
                CFLAGS="$CFLAGS $option"
                AC_MSG_CHECKING([whether gcc understands $option])
                AC_TRY_COMPILE([], [],
                        has_option=yes,
                        has_option=no,)
                if test $has_option = no; then
                        CFLAGS="$SAVE_CFLAGS"
                fi
                AC_MSG_RESULT($has_option)
                unset has_option
                unset SAVE_CFLAGS
        done
        unset option
else
        AC_MSG_RESULT(no)
fi

#
# Enable Debug
#
AC_ARG_ENABLE(debug,
	[AC_HELP_STRING([--enable-debug],
	[turn on debugging])],
	, enable_debug=yes)
if test "$enable_debug" = "yes"; then
	DEBUG_CFLAGS="-DG_ENABLE_DEBUG"
else
	if test "x$enable_debug" = "xno"; then
		DEBUG_CFLAGS="-DG_DISABLE_ASSERT -DG_DISABLE_CHECKS"
	else
		DEBUG_CFLAGS=""
	fi
fi
AC_SUBST(DEBUG_CFLAGS)

AC_OUTPUT([
Makefile
gnome-settings-daemon/Makefile
plugins/Makefile
plugins/a11y-keyboard/Makefile
plugins/a11y-settings/Makefile
plugins/clipboard/Makefile
plugins/color/Makefile
plugins/common/Makefile
plugins/datetime/Makefile
plugins/dummy/Makefile
plugins/power/Makefile
plugins/housekeeping/Makefile
plugins/keyboard/Makefile
plugins/media-keys/Makefile
plugins/media-keys/gvc/Makefile
plugins/mouse/Makefile
plugins/orientation/Makefile
plugins/print-notifications/Makefile
plugins/rfkill/Makefile
plugins/screensaver-proxy/Makefile
plugins/sharing/Makefile
plugins/smartcard/Makefile
plugins/sound/Makefile
plugins/wacom/Makefile
plugins/xrandr/Makefile
plugins/xsettings/Makefile
data/Makefile
data/gnome-settings-daemon.pc
data/gnome-settings-daemon-uninstalled.pc
data/org.gnome.settings-daemon.plugins.gschema.xml.in
data/org.gnome.settings-daemon.plugins.xsettings.gschema.xml.in
data/org.gnome.settings-daemon.plugins.keyboard.gschema.xml.in
data/org.gnome.settings-daemon.plugins.power.gschema.xml.in
data/org.gnome.settings-daemon.plugins.color.gschema.xml.in
data/org.gnome.settings-daemon.plugins.datetime.gschema.xml.in
data/org.gnome.settings-daemon.plugins.media-keys.gschema.xml.in
data/org.gnome.settings-daemon.peripherals.gschema.xml.in
data/org.gnome.settings-daemon.plugins.housekeeping.gschema.xml.in
data/org.gnome.settings-daemon.plugins.orientation.gschema.xml.in
data/org.gnome.settings-daemon.plugins.sharing.gschema.xml.in
data/org.gnome.settings-daemon.plugins.xrandr.gschema.xml.in
data/org.gnome.settings-daemon.peripherals.wacom.gschema.xml.in
data/org.gnome.settings-daemon.plugins.print-notifications.gschema.xml.in
po/Makefile.in
man/Makefile
tests/Makefile
])

dnl ---------------------------------------------------------------------------
dnl - Show summary
dnl ---------------------------------------------------------------------------

echo "
                    gnome-settings-daemon $VERSION
                    =============================

        prefix:                   ${prefix}
        exec_prefix:              ${exec_prefix}
        libdir:                   ${libdir}
        bindir:                   ${bindir}
        sbindir:                  ${sbindir}
        sysconfdir:               ${sysconfdir}
        sysconfsubdir:            ${sysconfsubdir}
        localstatedir:            ${localstatedir}
        plugindir:                ${plugindir}
        datadir:                  ${datadir}
        source code location:     ${srcdir}
        compiler:                 ${CC}
        cflags:                   ${CFLAGS}
        Maintainer mode:          ${USE_MAINTAINER_MODE}

        NetworkManager support:   ${enable_network_manager}
        Libnotify support:        ${have_libnotify}
        Smartcard support:        ${have_smartcard_support}
        Cups support:             ${enable_cups}
        Wacom support:            ${have_wacom}
        RFKill support:           ${enable_rfkill}
${NSS_DATABASE:+\
        System nssdb:             ${NSS_DATABASE}
}\
        Profiling support:        ${enable_profiling}
"
