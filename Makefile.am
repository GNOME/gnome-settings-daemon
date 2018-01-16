NULL =

SUBDIRS = 			\
	gnome-settings-daemon	\
	data			\
	plugins			\
	po 			\
	tests 			\
	$(NULL)

AM_DISTCHECK_CONFIGURE_FLAGS = --with-systemduserunitdir='$${libdir}/systemd/user-distcheck'

# Honor aclocal flags
ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

EXTRA_DIST = \
	MAINTAINERS		\
	ChangeLog		\
	README			\
	$(NULL)

DISTCLEANFILES = \
	$(NULL)
