AC_PREREQ([2.60])

m4_define([gsd_api_version_major],[2])
m4_define([gsd_api_version_minor],[0])
m4_define([gsd_api_version],[gsd_api_version_major.gsd_api_version_minor])

AC_INIT([gnome-settings-daemon],
        [2.24.0],
        [http://bugzilla.gnome.org/enter_bug.cgi?product=gnome-settings-daemon])

AC_CONFIG_SRCDIR([gnome-settings-daemon/gnome-settings-manager.c])

AM_INIT_AUTOMAKE([1.9 no-dist-gzip dist-bzip2 tar-ustar])

AM_MAINTAINER_MODE

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
GLIB_REQUIRED_VERSION=2.17.3
GTK_REQUIRED_VERSION=2.10.0
GCONF_REQUIRED_VERSION=2.6.1
GIO_REQUIRED_VERSION=2.17.3
GNOME_DESKTOP_REQUIRED_VERSION=2.23.90
LIBNOTIFY_REQUIRED_VERSION=0.4.3

EXTRA_COMPILE_WARNINGS(yes)

PKG_CHECK_MODULES(SETTINGS_DAEMON,
        glib-2.0 >= $GLIB_REQUIRED_VERSION
        gtk+-2.0 >= $GTK_REQUIRED_VERSION
        gconf-2.0 >= $GCONF_REQUIRED_VERSION
        gmodule-2.0
        dbus-glib-1 >= $DBUS_GLIB_REQUIRED_VERSION
        libgnome-2.0
)

PKG_CHECK_MODULES(SETTINGS_PLUGIN,
        gtk+-2.0 >= $GTK_REQUIRED_VERSION
        gconf-2.0 >= $GCONF_REQUIRED_VERSION
        gnome-desktop-2.0 >= $GNOME_DESKTOP_REQUIRED_VERSION
        gio-2.0 >= $GIO_REQUIRED_VERSION
        libglade-2.0 >= 2.0.0
        dbus-glib-1 >= $DBUS_GLIB_REQUIRED_VERSION
)

PKG_CHECK_MODULES(GNOME, libgnome-2.0)
PKG_CHECK_MODULES(LIBSOUNDS, gio-2.0 gtk+-2.0 libgnomeui-2.0)

GSD_PLUGIN_LDFLAGS="-export_dynamic -module -avoid-version -no-undefined"
AC_SUBST([GSD_PLUGIN_LDFLAGS])

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)
AC_PATH_XTRA

AC_PATH_PROG(GCONFTOOL, gconftool-2)

AM_GCONF_SOURCE_2

dnl ---------------------------------------------------------------------------
dnl - Check for libnotify
dnl ---------------------------------------------------------------------------

PKG_CHECK_MODULES(LIBNOTIFY, libnotify >= $LIBNOTIFY_REQUIRED_VERSION,
		  [AC_DEFINE(HAVE_LIBNOTIFY, 1, [Define if libnotify is available])
                  have_libnotify=yes], [have_libnotify=no])
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

# Like AC_CHECK_HEADER, but it uses the already-computed -I directories.
#
AC_DEFUN([AC_CHECK_X_HEADER], [
  ac_save_CPPFLAGS="$CPPFLAGS"
  if test \! -z "$includedir" ; then
    CPPFLAGS="$CPPFLAGS -I$includedir"
  fi
  CPPFLAGS="$CPPFLAGS $X_CFLAGS"
  AC_CHECK_HEADER([$1],[$2],[$3],[$4])
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
AC_CHECK_LIB(Xi, XListInputDevices,
  [AC_CHECK_HEADER(X11/extensions/XInput.h,
     have_xinput=yes
     XINPUT_LIBS="-lXi"
     AC_DEFINE(HAVE_XINPUT, 1, [Define if XInput extension is available]),
	  :, [#include <X11/Xlib.h>])], : ,
       -lXi $x_libs)
AM_CONDITIONAL(HAVE_XINPUT, [test $have_xinput = yes])
AC_SUBST(XINPUT_LIBS)

dnl ---------------------------------------------------------------------------
dnl - XRandR
dnl ---------------------------------------------------------------------------

have_randr=no
AC_CHECK_LIB(Xrandr, XRRUpdateConfiguration,
  [AC_CHECK_HEADER(X11/extensions/Xrandr.h,
     have_randr=yes
     RANDR_LIBS="-lXrandr -lXrender"
     AC_DEFINE(HAVE_RANDR, 1, [Define if Xrandr extension is available]),
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

AC_CHECK_HEADERS([X11/Xlib.h])

AC_CHECK_X_LIB(Xxf86misc, XF86MiscQueryExtension, [
  AC_CHECK_HEADERS([X11/extensions/xf86misc.h], [XF86MISC_LIBS="-lXxf86misc"],[],
[#if HAVE_X11_XLIB_H
#include <X11/Xlib.h>
#endif
])], [true], -lXext -lX11)
AC_SUBST(XF86MISC_LIBS)
AC_CHECK_HEADERS(X11/extensions/XKB.h)

PKG_CHECK_MODULES(LIBGNOMEKBD, [libgnomekbd >= 2.21.4 libxklavier >= 3.3])
AC_SUBST(LIBGNOMEKBD_CFLAGS)
AC_SUBST(LIBGNOMEKBD_LIBS)

dnl ==============================================
dnl Esd section
dnl ==============================================

AC_ARG_ENABLE(esd,
  AC_HELP_STRING([--disable-esd],
                 [turn off ESD audio support]),
       [case "${enableval}" in
               yes) WANT_ESD=yes ;;
               no)  WANT_ESD=no ;;
               *) AC_MSG_ERROR(bad value ${enableval} for --disable-esd) ;;
       esac],
       [WANT_ESD=yes]) dnl Default value

if test x$WANT_ESD = xyes ; then
       PKG_CHECK_MODULES(ESD, esound >= 0.2.28,
	       AC_DEFINE(HAVE_ESD, 1, [Define if ESD sound server should be used]),
               AC_MSG_RESULT([disabled]))

       GSD_ESD_LOCATION=`pkg-config esound --variable=esd_serverdir`
       if test "x$GSD_ESD_LOCATION" != "x"; then
           # Add slash so that if the ESD pkgconfig file does not
           # specify esd_serverdir, g-s-d will look in the PATH
           GSD_ESD_LOCATION="$GSD_ESD_LOCATION/"
       fi
fi

AC_SUBST(ESD_CFLAGS)
AC_SUBST(ESD_LIBS)
AC_SUBST(ESD_LOCATION)

dnl ==============================================
dnl GStreamer section
dnl ==============================================
GST_MAJORMINOR=auto

AC_ARG_ENABLE(gstreamer,
AC_HELP_STRING([--enable-gstreamer],[use gstreamer if available (and optionally specify a version)]),
[case "${enableval}" in
 yes) ENABLE_GSTREAMER=yes ;;
 0.10) ENABLE_GSTREAMER=yes && GST_MAJORMINOR=0.10 ;;
 no)  ENABLE_GSTREAMER=no ;;
 *) AC_MSG_ERROR([
                  *** Bad value ${enableval} for --enable-gstreamer
                  *** Please use one of the following:
                  ***    --enable-gstreamer=0.10
               ]) ;;
esac],
[ENABLE_GSTREAMER=yes]) dnl Default value

have_gstreamer=no
if test "x$ENABLE_GSTREAMER" = "xyes"; then
   GST_REQS=0.10.1.2
   PKGS="gstreamer-0.10 >= $GST_REQS gstreamer-plugins-base-0.10 >= $GST_REQS"

   PKG_CHECK_MODULES(GST, $PKGS, have_gstreamer=yes,
                     AC_MSG_RESULT([no]))

   if test "x$have_gstreamer" = "xyes"; then
      GST_LIBS="$GST_LIBS -lgstinterfaces-0.10 -lgstaudio-0.10"
   fi
else
   AC_MSG_NOTICE([*** GStreamer support disabled ***])
fi
AM_CONDITIONAL(HAVE_GSTREAMER, test "x$have_gstreamer" = "xyes")
AC_SUBST(GST_LIBS)
AC_SUBST(GST_CFLAGS)

dnl ==============================================
dnl OSS section
dnl ==============================================

have_oss=no
if test "x$have_gstreamer" != "xyes"; then
  AC_TRY_COMPILE([
		  #ifdef __NetBSD__
		  #include <sys/param.h>
		  #include <sys/sysctl.h>
		  #include <soundcard.h>
		  #else
		  #include <sys/soundcard.h>
		  #endif
		  ],[
		     int arg = SNDCTL_DSP_SETFRAGMENT;
		     ],[
			have_oss=yes
			])
  AC_MSG_CHECKING(for OSS audio support)
  AC_MSG_RESULT($have_oss)
fi

AM_CONDITIONAL(HAVE_OSS, test x"$have_oss" = "xyes")

dnl ==============================================
dnl ALSA section
dnl ==============================================

have_alsa=no
if test "x$have_gstreamer" != "xyes"; then
  AC_ARG_ENABLE(alsa,
		AC_HELP_STRING([--disable-alsa],
			       [turn off ALSA audio support]),
			       [case "${enableval}" in
				yes) WANT_ALSA=yes ;;
				no)  WANT_ALSA=no ;;
				*) AC_MSG_ERROR(bad value ${enableval} for --disable-alsa) ;;
			esac],
			[WANT_ALSA=yes]) dnl Default value

  if test x$WANT_ALSA = xyes ; then
	  PKG_CHECK_MODULES(ALSA, alsa >= 0.9.0,
			    have_alsa=yes,
			    AC_MSG_RESULT([disabled]))
  fi

  AC_SUBST(ALSA_LIBS)
  AC_SUBST(ALSA_CFLAGS)
fi

AM_CONDITIONAL(HAVE_ALSA, test x"$have_alsa" = "xyes")

# ---------------------------------------------------------------------------
# Enable Legacy Sound Preference
# ---------------------------------------------------------------------------
PKG_CHECK_MODULES(PULSEAUDIO, libpulse, have_pulseaudio=yes, have_pulseaudio=no)
if test x$have_pulseaudio = xyes; then
	default_enable_legacy_sound_pref=no
else
	default_enable_legacy_sound_pref=yes
fi
AC_ARG_ENABLE(legacy-sound-pref,
	[AC_HELP_STRING([--enable-legacy-sound-pref],
	[check for preference to start ESD])],
	,enable_legacy_sound_pref=$default_enable_legacy_sound_pref)
if test "x$enable_legacy_sound_pref" = "xyes"; then
    AC_DEFINE(ENABLE_LEGACY_SOUND_PREF,1,[enable preference for ESD startup])
fi

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
plugins/media-keys/actions/Makefile
plugins/mouse/Makefile
plugins/screensaver/Makefile
plugins/sound/Makefile
plugins/sound/libsounds/Makefile
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

        OSS support:              ${have_oss}
        ALSA support:             ${have_alsa}
        GStreamer support:        ${have_gstreamer}
        ESD support:              ${WANT_ESD}

        Profiling support:        ${enable_profiling}
"
