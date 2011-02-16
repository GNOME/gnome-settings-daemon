AC_PREREQ([2.60])

AC_INIT([gnome-settings-daemon],
        [2.91.10],
        [http://bugzilla.gnome.org/enter_bug.cgi?product=gnome-settings-daemon])

AC_CONFIG_SRCDIR([gnome-settings-daemon/gnome-settings-manager.c])

AM_INIT_AUTOMAKE([1.9 no-dist-gzip dist-bzip2 tar-ustar])
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

AS_AC_EXPAND(LIBEXECDIR, "$libexecdir")

GSD_INTLTOOL_PLUGIN_RULE='%.gnome-settings-plugin:   %.gnome-settings-plugin.in   $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*.po) ; LC_ALL=C $(INTLTOOL_MERGE) -d -u -c $(top_builddir)/po/.intltool-merge-cache $(top_srcdir)/po $< [$]@'
AC_SUBST([GSD_INTLTOOL_PLUGIN_RULE])

dnl ---------------------------------------------------------------------------
dnl - Dependencies
dnl ---------------------------------------------------------------------------

DBUS_GLIB_REQUIRED_VERSION=0.74
GLIB_REQUIRED_VERSION=2.26.0
GTK_REQUIRED_VERSION=2.99.3
GCONF_REQUIRED_VERSION=2.6.1
GIO_REQUIRED_VERSION=2.26.0
GNOME_DESKTOP_REQUIRED_VERSION=2.91.5
LIBNOTIFY_REQUIRED_VERSION=0.6.1

EXTRA_COMPILE_WARNINGS(yes)

PKG_CHECK_MODULES(SETTINGS_DAEMON,
        glib-2.0 >= $GLIB_REQUIRED_VERSION
        gtk+-3.0 >= $GTK_REQUIRED_VERSION
	gio-2.0 >= $GIO_REQUIRED_VERSION
        gmodule-2.0
        gthread-2.0
        dbus-glib-1 >= $DBUS_GLIB_REQUIRED_VERSION
	gsettings-desktop-schemas >= 0.1.2
)

PKG_CHECK_MODULES(SETTINGS_PLUGIN,
        gtk+-3.0 >= $GTK_REQUIRED_VERSION
        gconf-2.0 >= $GCONF_REQUIRED_VERSION
        gio-2.0 >= $GIO_REQUIRED_VERSION
        dbus-glib-1 >= $DBUS_GLIB_REQUIRED_VERSION
        gsettings-desktop-schemas
)

GSD_PLUGIN_LDFLAGS="-export_dynamic -module -avoid-version -no-undefined"
case $host_os in
  darwin*)
    GSD_PLUGIN_LDFLAGS="${GSD_PLUGIN_LDFLAGS} -Wl,-bundle_loader,\$(top_builddir)/gnome-settings-daemon/gnome-settings-daemon"
    ;;
esac
AC_SUBST([GSD_PLUGIN_LDFLAGS])

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)

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
dnl - Check for D-Bus
dnl ---------------------------------------------------------------------------

dnl - Are we specifying a different dbus root ?
AC_ARG_WITH(dbus-sys,
        [AC_HELP_STRING([--with-dbus-sys=<dir>],
        [where D-BUS system.d directory is])])
AC_ARG_WITH(dbus-services,
        [AC_HELP_STRING([--with-dbus-services=<dir>],
        [where D-BUS services directory is])])
if ! test -z "$with_dbus_sys" ; then
        DBUS_SYS_DIR="$with_dbus_sys"
else
        DBUS_SYS_DIR='${sysconfdir}/dbus-1/system.d'
fi
AC_SUBST(DBUS_SYS_DIR)

dnl ---------------------------------------------------------------------------
dnl - X11 stuff
dnl ---------------------------------------------------------------------------

# Like AC_CHECK_HEADERS, but it uses the already-computed -I directories.
#
AC_DEFUN([AC_CHECK_X_HEADERS], [
  ac_save_CPPFLAGS="$CPPFLAGS"
  if test \! -z "$includedir" ; then
    CPPFLAGS="$CPPFLAGS -I$includedir"
  fi
  CPPFLAGS="$CPPFLAGS $X_CFLAGS"
  AC_CHECK_HEADERS([$1],[$2],[$3],[$4])
  CPPFLAGS="$ac_save_CPPFLAGS"])

# Like AC_CHECK_LIB, but it uses the already-computed -I and -L directories.
# Use this sparingly; it probably doesn't work very well on X programs.
#
AC_DEFUN([AC_CHECK_X_LIB], [
  ac_save_CPPFLAGS="$CPPFLAGS"
  ac_save_LDFLAGS="$LDFLAGS"
#  ac_save_LIBS="$LIBS"

  if test \! -z "$includedir" ; then
    CPPFLAGS="$CPPFLAGS -I$includedir"
  fi
  # note: $X_CFLAGS includes $x_includes
  CPPFLAGS="$CPPFLAGS $X_CFLAGS"

  if test \! -z "$libdir" ; then
    LDFLAGS="$LDFLAGS -L$libdir"
  fi
  # note: $X_LIBS includes $x_libraries
  LDFLAGS="$LDFLAGS $ALL_X_LIBS"

  AC_CHECK_LIB([$1], [$2], [$3], [$4], [$5])
  CPPFLAGS="$ac_save_CPPFLAGS"
  LDFLAGS="$ac_save_LDFLAGS"
#  LIBS="$ac_save_LIBS"
  ])

AC_PATH_XTRA

ALL_X_LIBS="$X_LIBS $X_PRE_LIBS -lXext -lX11 $X_EXTRA_LIBS"

X11_LIBS="$ALL_X_LIBS"
AC_SUBST(X11_LIBS)

dnl ---------------------------------------------------------------------------
dnl - XInput
dnl ---------------------------------------------------------------------------

PKG_CHECK_MODULES(XINPUT, xi)

dnl ---------------------------------------------------------------------------
dnl - Fontconfig
dnl ---------------------------------------------------------------------------

PKG_CHECK_MODULES(FONTCONFIG, fontconfig)

dnl ---------------------------------------------------------------------------
dnl - Keyboard plugin stuff
dnl ---------------------------------------------------------------------------

AC_CHECK_X_LIB(Xxf86misc, XF86MiscQueryExtension, [
  AC_CHECK_X_HEADERS([X11/extensions/xf86misc.h],
      [XF86MISC_LIBS="-lXxf86misc"], :,
      [#include <X11/Xlib.h>])], : ,
      -lXxf86misc $x_libs)
AC_SUBST(XF86MISC_LIBS)
AC_CHECK_X_HEADERS([X11/extensions/XKB.h])

LIBGNOMEKBD_REQUIRED=2.91.1
PKG_CHECK_MODULES(LIBGNOMEKBDUI, [libgnomekbdui >= $LIBGNOMEKBD_REQUIRED libgnomekbd >= $LIBGNOMEKBD_REQUIRED libxklavier >= 5.0])
AC_SUBST(LIBGNOMEKBDUI_CFLAGS)
AC_SUBST(LIBGNOMEKBDUI_LIBS)

dnl ---------------------------------------------------------------------------
dnl - Housekeeping plugin stuff
dnl ---------------------------------------------------------------------------

PKG_CHECK_MODULES(GIOUNIX, [gio-unix-2.0])
AC_SUBST(GIOUNIX_CFLAGS)
AC_SUBST(GIOUNIX_LIBS)

dnl ---------------------------------------------------------------------------
dnl - GConf<->GSettings bridge
dnl ---------------------------------------------------------------------------

build_gconf_bridge=false
AC_ARG_ENABLE(gconf-bridge,
  AC_HELP_STRING([--disable-gconf-bridge],
                 [turn off GConf bridge plugin]),
      [case "${enableval}" in
              yes) WANT_GCONF_BRIDGE=yes ;;
	      no) WANT_GCONF_BRIDGE=no ;;
	      *) AC_MSG_ERROR(bad value ${enableval} for --disable-gconf-bridge) ;;
      esac],
      [WANT_GCONF_BRIDGE=no]) dnl Default value

if test x$WANT_GCONF_BRIDGE = xyes; then
      PKG_CHECK_MODULES(GCONF, gconf-2.0 >= 2.6.1,
            [build_gconf_bridge=true
             AC_DEFINE(BUILD_GCONF_BRIDGE, 1, [Define if GConf bridge should be built])],
	    [build_gconf_bridge=false])
fi
AM_CONDITIONAL(BUILD_GCONF_BRIDGE, test "x$build_gconf_bridge" = "xtrue")

AC_SUBST(GCONF_CFLAGS)
AC_SUBST(GCONF_LIBS)

dnl ==============================================
dnl PulseAudio section
dnl ==============================================


have_pulse=false
AC_ARG_ENABLE(pulse,
  AC_HELP_STRING([--disable-pulse],
                 [turn off PulseAudio support]),
       [case "${enableval}" in
               yes) WANT_PULSE=yes ;;
               no)  WANT_PULSE=no ;;
               *) AC_MSG_ERROR(bad value ${enableval} for --disable-pulse) ;;
       esac],
       [WANT_PULSE=yes]) dnl Default value

if test x$WANT_PULSE = xyes ; then
       PA_REQUIRED_VERSION=0.9.16
       PKG_CHECK_MODULES(PULSE, glib-2.0 libpulse >= $PA_REQUIRED_VERSION libpulse-mainloop-glib >= $PA_REQUIRED_VERSION libcanberra-gtk3,
             [have_pulse=true
              AC_DEFINE(HAVE_PULSE, 1, [Define if PULSE sound server should be used])],
             [have_pulse=false])
fi
AM_CONDITIONAL(HAVE_PULSE, test "x$have_pulse" = "xtrue")

AC_SUBST(PULSE_CFLAGS)
AC_SUBST(PULSE_LIBS)


dnl ==============================================
dnl PackageKit section
dnl ==============================================

have_packagekit=false
AC_ARG_ENABLE(packagekit,
  AC_HELP_STRING([--disable-packagekit],
                 [turn off PackageKit support]),
       [case "${enableval}" in
               yes) WANT_PACKAGEKIT=yes ;;
               no)  WANT_PACKAGEKIT=no ;;
               *) AC_MSG_ERROR(bad value ${enableval} for --disable-packagekit) ;;
       esac],
       [WANT_PACKAGEKIT=yes]) dnl Default value

if test x$WANT_PACKAGEKIT = xyes ; then
       PK_REQUIRED_VERSION=0.6.4
       PKG_CHECK_MODULES(PACKAGEKIT, glib-2.0 packagekit-glib2 >= $PK_REQUIRED_VERSION upower-glib >= 0.9.1,
             [have_packagekit=true
              AC_DEFINE(HAVE_PACKAGEKIT, 1, [Define if PackageKit should be used])],
             [have_packagekit=false])
fi
AM_CONDITIONAL(HAVE_PACKAGEKIT, test "x$have_packagekit" = "xtrue")

AC_SUBST(PACKAGEKIT_CFLAGS)
AC_SUBST(PACKAGEKIT_LIBS)

dnl ---------------------------------------------------------------------------
dnl - GUdev integration (default enabled)
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(gudev, AS_HELP_STRING([--disable-gudev],[Disable GUdev support]), enable_gudev=$enableval)
if test x$enable_gudev != xno; then
	PKG_CHECK_MODULES(GUDEV, gudev-1.0, HAVE_GUDEV="yes", HAVE_GUDEV="no")
	if test "x$HAVE_GUDEV" = "xyes"; then
		AC_DEFINE(HAVE_GUDEV, 1, [define if GUdev is available])
	else
		if test x$enable_gudev = xyes; then
			AC_MSG_ERROR([GUdev enabled but not found])
		fi
	fi
else
	HAVE_GUDEV=no
fi
AM_CONDITIONAL(HAVE_GUDEV, test x$HAVE_GUDEV = xyes)


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
# PolicyKit
# ---------------------------------------------------------------------------

POLKIT_REQUIRED=0.97
DBUS_REQUIRED=1.1.2

# PolicyKit detection; defaults to 'auto' (use it if it's available)
#
POLKIT_CFLAGS=
POLKIT_LIBS=
AC_ARG_ENABLE(polkit,
              AS_HELP_STRING([--enable-polkit],
                             [Enable PolicyKit support (auto)]),
              enable_polkit=$enableval,
              enable_polkit=auto)

if test "x$enable_polkit" = "xno" ; then
  HAVE_POLKIT=no
else
  HAVE_POLKIT=no
  PKG_CHECK_MODULES(POLKIT, polkit-gobject-1 >= $POLKIT_REQUIRED dbus-1 >= $DBUS_REQUIRED, HAVE_POLKIT=yes, HAVE_POLKIT=no)

  if test "x$enable_polkit" = "xyes" -a "x$HAVE_POLKIT" = "xno" ; then
    AC_MSG_ERROR(PolicyKit support explicity enabled but not available)
  fi

  if test "x$HAVE_POLKIT" = "xyes" ; then
    AC_DEFINE(HAVE_POLKIT, 1, [Defined if PolicyKit support is enabled])
  fi
fi
AM_CONDITIONAL(HAVE_POLKIT, test "x$HAVE_POLKIT" = "xyes")
AC_SUBST(POLKIT_CFLAGS)
AC_SUBST(POLKIT_LIBS)


# ---------------------------------------------------------------------------
# CUPS
# ---------------------------------------------------------------------------

AC_PATH_PROG(CUPS_CONFIG, cups-config, no)
if test "x$CUPS_CONFIG" != "xno"; then
  CUPS_API_VERSION=`$CUPS_CONFIG --api-version`
  CUPS_API_MAJOR=`echo $ECHO_N $CUPS_API_VERSION | sed -e "s/\./\n/g" | sed -n "1p"`
  CUPS_API_MINOR=`echo $ECHO_N $CUPS_API_VERSION | sed -e "s/\./\n/g" | sed -n "2p"`

  AC_CHECK_HEADERS([cups/cups.h cups/http.h cups/ipp.h], have_cups_headers=yes, have_cups_headers=no)
  if test x$have_cups_headers = xyes ; then
    if test $CUPS_API_MAJOR -gt 1 -o \
            $CUPS_API_MAJOR -eq 1 -a $CUPS_API_MINOR -ge 4; then
      have_cups=yes
      CUPS_LIBS=-lcups
      AC_SUBST(CUPS_LIBS)
    else
      have_cups=no
      AC_MSG_WARN(*** Print-notifications plugin will not be built (CUPS 1.4 or newer not found) ***)
    fi
  else
    have_cups=no
    AC_MSG_WARN(*** Print-notifications plugin will not be built (CUPS header files not found) ***)
  fi
else
  have_cups=no
  AC_MSG_WARN(*** Print-notifications plugin will not be built (cups-config not found) ***)
fi
AM_CONDITIONAL(BUILD_PRINT_NOTIFICATIONS, [test x$have_cups = xyes])


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
plugins/automount/Makefile
plugins/background/Makefile
plugins/clipboard/Makefile
plugins/common/Makefile
plugins/datetime/Makefile
plugins/dummy/Makefile
plugins/gconf/Makefile
plugins/housekeeping/Makefile
plugins/keybindings/Makefile
plugins/keyboard/Makefile
plugins/media-keys/Makefile
plugins/media-keys/cut-n-paste/Makefile
plugins/mouse/Makefile
plugins/print-notifications/Makefile
plugins/smartcard/Makefile
plugins/sound/Makefile
plugins/updates/Makefile
plugins/wacom/Makefile
plugins/xrandr/Makefile
plugins/xsettings/Makefile
data/Makefile
data/gnome-settings-daemon.desktop.in
data/gnome-settings-daemon.pc
data/gnome-settings-daemon-uninstalled.pc
data/org.gnome.settings-daemon.plugins.gschema.xml.in
data/org.gnome.settings-daemon.plugins.xsettings.gschema.xml.in
data/org.gnome.settings-daemon.plugins.keyboard.gschema.xml.in
data/org.gnome.settings-daemon.plugins.power.gschema.xml.in
data/org.gnome.settings-daemon.plugins.media-keys.gschema.xml.in
data/org.gnome.settings-daemon.peripherals.gschema.xml.in
data/org.gnome.settings-daemon.plugins.housekeeping.gschema.xml.in
data/org.gnome.settings-daemon.plugins.updates.gschema.xml.in
data/org.gnome.settings-daemon.plugins.xrandr.gschema.xml.in
data/org.gnome.settings-daemon.peripherals.wacom.gschema.xml.in
data/org.gnome.settings-daemon.plugins.print-notifications.gschema.xml.in
po/Makefile.in
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

        dbus-1 system.d dir:      ${DBUS_SYS_DIR}
        PolicyKit support:        ${HAVE_POLKIT}

        Libnotify support:        ${have_libnotify}
        PulseAudio support:       ${have_pulse}
        PackageKit support:       ${have_packagekit}
        Smartcard support:        ${have_smartcard_support}
        GConf bridge support:     ${build_gconf_bridge}
        Cups support:             ${have_cups}
${NSS_DATABASE:+\
        System nssdb:             ${NSS_DATABASE}
}\
        Profiling support:        ${enable_profiling}
"
