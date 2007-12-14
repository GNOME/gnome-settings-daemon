
AC_PREREQ([2.60])
AC_INIT([gnome-settings-daemon],
        [2.21.2],
        [http://bugzilla.gnome.org/enter_bug.cgi?product=gnome-settings-daemon])

AC_CONFIG_SRCDIR([src/gnome-settings-manager.c])

AM_INIT_AUTOMAKE([1.9])

AM_MAINTAINER_MODE

AC_STDC_HEADERS
AC_PROG_CXX
AM_PROG_CC_C_O
AC_PROG_LIBTOOL()

AC_HEADER_STDC

AC_SUBST(VERSION)

AM_CONFIG_HEADER(config.h)

IT_PROG_INTLTOOL([0.35.0])

GETTEXT_PACKAGE=gnome-settings-daemon
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Name of default gettext domain])

AM_GLIB_GNU_GETTEXT

dnl ---------------------------------------------------------------------------
dnl - Dependencies
dnl ---------------------------------------------------------------------------

DBUS_GLIB_REQUIRED_VERSION=0.74
GLIB_REQUIRED_VERSION=2.13.0
GTK_REQUIRED_VERSION=2.10.0
GCONF_REQUIRED_VERSION=2.6.1

EXTRA_COMPILE_WARNINGS(yes)

PKG_CHECK_MODULES(SETTINGS_DAEMON,
        gtk+-2.0 >= $GTK_REQUIRED_VERSION
        gconf-2.0 >= $GCONF_REQUIRED_VERSION
        gmodule-2.0
        dbus-glib-1 >= $DBUS_GLIB_REQUIRED_VERSION
)
AC_SUBST(SETTINGS_DAEMON_CFLAGS)
AC_SUBST(SETTINGS_DAEMON_LIBS)

PKG_CHECK_MODULES(SETTINGS_PLUGIN,
        gtk+-2.0 >= $GTK_REQUIRED_VERSION
        gconf-2.0 >= $GCONF_REQUIRED_VERSION
)
AC_SUBST(SETTINGS_PLUGIN_CFLAGS)
AC_SUBST(SETTINGS_PLUGIN_LIBS)

PLUGIN_LIBTOOL_FLAGS="-export_dynamic -module -avoid-version"
AC_SUBST(PLUGIN_LIBTOOL_FLAGS)

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)
AC_PATH_XTRA

AC_PATH_PROG(GCONFTOOL, gconftool-2)

AM_GCONF_SOURCE_2

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
        DBUS_SYS_DIR="$sysconfdir/dbus-1/system.d"
fi
AC_SUBST(DBUS_SYS_DIR)


dnl ---------------------------------------------------------------------------
dnl - Expand vars
dnl ---------------------------------------------------------------------------

AS_AC_EXPAND(LOCALSTATEDIR, $localstatedir)
AS_AC_EXPAND(SYSCONFDIR, $sysconfdir)
AS_AC_EXPAND(DATADIR, $datadir)
AS_AC_EXPAND(LIBDIR, $libdir)
AS_AC_EXPAND(LIBEXECDIR, $libexecdir)

dnl ---------------------------------------------------------------------------
dnl - Finish
dnl ---------------------------------------------------------------------------


# Turn on the additional warnings last, so -Werror doesn't affect other tests.

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
        -Wcast-align -Wsign-compare -Werror \
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
src/Makefile
plugins/Makefile
plugins/clipboard/Makefile
plugins/dummy/Makefile
plugins/xrandr/Makefile
plugins/xrdb/Makefile
plugins/xrdb/data/Makefile
plugins/xsettings/Makefile
data/Makefile
data/gnome-settings-daemon.pc
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
        datadir:                  ${datadir}
	source code location:	  ${srcdir}
	compiler:		  ${CC}
	cflags:		          ${CFLAGS}
        Maintainer mode:          ${USE_MAINTAINER_MODE}

        dbus-1 system.d dir:      ${DBUS_SYS_DIR}
"
