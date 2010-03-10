AC_PREREQ([2.60])

m4_define([gsd_api_version_major],[2])
m4_define([gsd_api_version_minor],[0])
m4_define([gsd_api_version],[gsd_api_version_major.gsd_api_version_minor])

AC_INIT([gnome-settings-daemon],
        [2.28.2],
        [http://bugzilla.gnome.org/enter_bug.cgi?product=gnome-settings-daemon])

AC_CONFIG_SRCDIR([gnome-settings-daemon/gnome-settings-manager.c])

AM_INIT_AUTOMAKE([1.9 no-dist-gzip dist-bzip2 tar-ustar])

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

 dnl Unconditionally use this dir to avoid a circular dep with gnomecc
GNOME_KEYBINDINGS_KEYSDIR="${datadir}/gnome-control-center/keybindings"
AC_SUBST(GNOME_KEYBINDINGS_KEYSDIR)

dnl ---------------------------------------------------------------------------
dnl - Dependencies
dnl ---------------------------------------------------------------------------

DBUS_GLIB_REQUIRED_VERSION=0.74
GLIB_REQUIRED_VERSION=2.17.3
GTK_REQUIRED_VERSION=2.16.0
GCONF_REQUIRED_VERSION=2.6.1
GIO_REQUIRED_VERSION=2.17.3
GNOME_DESKTOP_REQUIRED_VERSION=2.26.3
LIBNOTIFY_REQUIRED_VERSION=0.4.3

EXTRA_COMPILE_WARNINGS(yes)

PKG_CHECK_MODULES(SETTINGS_DAEMON,
        glib-2.0 >= $GLIB_REQUIRED_VERSION
        gtk+-2.0 >= $GTK_REQUIRED_VERSION
        gconf-2.0 >= $GCONF_REQUIRED_VERSION
        gmodule-2.0
        dbus-glib-1 >= $DBUS_GLIB_REQUIRED_VERSION
)

PKG_CHECK_MODULES(SETTINGS_PLUGIN,
        gtk+-2.0 >= $GTK_REQUIRED_VERSION
        gconf-2.0 >= $GCONF_REQUIRED_VERSION
        gnome-desktop-2.0 >= $GNOME_DESKTOP_REQUIRED_VERSION
        gio-2.0 >= $GIO_REQUIRED_VERSION
        dbus-glib-1 >= $DBUS_GLIB_REQUIRED_VERSION
)

GSD_PLUGIN_LDFLAGS="-export_dynamic -module -avoid-version -no-undefined"
case $host_os in
  darwin*)
    GSD_PLUGIN_LDFLAGS="${GSD_PLUGIN_LDFLAGS} -Wl,-bundle_loader,\$(top_builddir)/gnome-settings-daemon/gnome-settings-daemon"
    ;;
esac
AC_SUBST([GSD_PLUGIN_LDFLAGS])

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)

AC_PATH_PROG(GCONFTOOL, gconftool-2)

AM_GCONF_SOURCE_2

dnl ---------------------------------------------------------------------------
dnl - Check for libnotify
dnl ---------------------------------------------------------------------------

have_libnotify=no
AC_ARG_WITH(libnotify,
	AC_HELP_STRING([--without-libnotify], [Disable notifications (default: auto)]),
	with_libnotify=$withval, with_libnotify=auto)

if test "x$with_libnotify" != "xno"; then
	PKG_CHECK_MODULES(LIBNOTIFY, libnotify >= $LIBNOTIFY_REQUIRED_VERSION,
			  [AC_DEFINE(HAVE_LIBNOTIFY, 1, [Define if libnotify is available])
			  have_libnotify=yes], have_libnotify=no)
	if test "x$have_libnotify" = xno -a "x$with_libnotify" = xyes; then
		AC_MSG_ERROR([libnotify support requested but libraries not found])
	fi
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

dnl ---------------------------------------------------------------------------
dnl - XInput
dnl ---------------------------------------------------------------------------

have_xinput=no
AC_CHECK_X_LIB(Xi, XListInputDevices,
  [AC_CHECK_X_HEADERS(X11/extensions/XInput.h,
    [AC_CHECK_TYPES([XDevicePresenceNotifyEvent], [
       have_xinput=yes
       XINPUT_LIBS="-lXi"],
       [], [#include <X11/extensions/XInput.h>])],
     [], [[#include <X11/Xlib.h>>]])],
  [], [-lXi $x_libs])

AM_CONDITIONAL(HAVE_XINPUT, [test $have_xinput = yes])
AC_SUBST(XINPUT_LIBS)

dnl ---------------------------------------------------------------------------
dnl - XRandR
dnl ---------------------------------------------------------------------------

have_randr=no
AC_CHECK_X_LIB(Xrandr, XRRUpdateConfiguration,
  [AC_CHECK_X_HEADERS(X11/extensions/Xrandr.h,
     have_randr=yes
     RANDR_LIBS="-lXrandr -lXrender",
	  :, [#include <X11/Xlib.h>])], : ,
       -lXrandr -lXrender $x_libs)
AC_SUBST(RANDR_LIBS)

dnl ---------------------------------------------------------------------------
dnl - Fontconfig
dnl ---------------------------------------------------------------------------

have_fontconfig=no
PKG_CHECK_MODULES(FONTCONFIG, fontconfig,
		  [AC_DEFINE(HAVE_FONTCONFIG, 1, [Define if Fontconfig functionality is available])
		   have_fontconfig=yes],
		  [have_fontconfig=no])
AM_CONDITIONAL(HAVE_FONTCONFIG, test x"$have_fontconfig" = "xyes")

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

PKG_CHECK_MODULES(LIBGNOMEKBD, [libgnomekbd >= 2.21.4 libxklavier >= 4.0])
AC_SUBST(LIBGNOMEKBD_CFLAGS)
AC_SUBST(LIBGNOMEKBD_LIBS)

dnl ---------------------------------------------------------------------------
dnl - Housekeeping plugin stuff
dnl ---------------------------------------------------------------------------

PKG_CHECK_MODULES(GIOUNIX, [gio-unix-2.0])
AC_SUBST(GIOUNIX_CFLAGS)
AC_SUBST(GIOUNIX_LIBS)

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
       PA_REQUIRED_VERSION=0.9.15
       PKG_CHECK_MODULES(PULSE, glib-2.0 libpulse >= $PA_REQUIRED_VERSION libpulse-mainloop-glib >= $PA_REQUIRED_VERSION libcanberra-gtk,
             [have_pulse=true
              AC_DEFINE(HAVE_PULSE, 1, [Define if PULSE sound server should be used])],
             [have_pulse=false])
fi
AM_CONDITIONAL(HAVE_PULSE, test "x$have_pulse" = "xtrue")

AC_SUBST(PULSE_CFLAGS)
AC_SUBST(PULSE_LIBS)

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
        	if test -d $srcdir/.svn; then
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
plugins/background/Makefile
plugins/clipboard/Makefile
plugins/common/Makefile
plugins/dummy/Makefile
plugins/font/Makefile
plugins/housekeeping/Makefile
plugins/keybindings/Makefile
plugins/keyboard/Makefile
plugins/media-keys/Makefile
plugins/media-keys/cut-n-paste/Makefile
plugins/mouse/Makefile
plugins/sound/Makefile
plugins/typing-break/Makefile
plugins/xrandr/Makefile
plugins/xrdb/Makefile
plugins/xrdb/data/Makefile
plugins/xsettings/Makefile
data/Makefile
data/gnome-settings-daemon.desktop.in
data/gnome-settings-daemon.pc
data/gnome-settings-daemon-uninstalled.pc
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
	source code location:	  ${srcdir}
	compiler:		  ${CC}
	cflags:		          ${CFLAGS}
        Maintainer mode:          ${USE_MAINTAINER_MODE}

        dbus-1 system.d dir:      ${DBUS_SYS_DIR}

        Libnotify support:        ${have_libnotify}
        PulseAudio support:       ${have_pulse}
        Profiling support:        ${enable_profiling}
"
