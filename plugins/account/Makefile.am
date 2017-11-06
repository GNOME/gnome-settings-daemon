plugin_name = account

dbus_accounts_built_sources = org.freedesktop.Accounts.c org.freedesktop.Accounts.h
dbus_accounts_user_built_sources = org.freedesktop.Accounts.User.c org.freedesktop.Accounts.User.h

BUILT_SOURCES = $(dbus_accounts_built_sources) \
		$(dbus_accounts_user_built_sources)

libexec_PROGRAMS = gsd-account

$(dbus_accounts_built_sources) : Makefile.am org.freedesktop.Accounts.xml
	$(AM_V_GEN) gdbus-codegen \
	--interface-prefix org.freedesktop. \
	--c-namespace Gsd \
	--generate-c-code org.freedesktop.Accounts \
	--annotate "org.freedesktop.Accounts"	\
		   "org.gtk.GDBus.C.Name" Accounts	\
	$(srcdir)/org.freedesktop.Accounts.xml

$(dbus_accounts_user_built_sources) : Makefile.am org.freedesktop.Accounts.User.xml
	$(AM_V_GEN) gdbus-codegen \
	--interface-prefix org.freedesktop. \
	--c-namespace Gsd \
	--generate-c-code org.freedesktop.Accounts.User \
	--annotate "org.freedesktop.Accounts.User"	\
		   "org.gtk.GDBus.C.Name" AccountsUser	\
	$(srcdir)/org.freedesktop.Accounts.User.xml

gsd_account_SOURCES =		\
	main.c			\
	gsd-account-manager.c	\
	gsd-account-manager.h	\
	$(COMMON_FILES)

nodist_gsd_account_SOURCES = \
	$(dbus_accounts_built_sources) \
	$(dbus_accounts_user_built_sources)

gsd_account_CPPFLAGS =					\
	-I$(top_srcdir)/data/					\
	-I$(top_srcdir)/gnome-settings-daemon			\
	-I$(top_builddir)/gnome-settings-daemon			\
	-I$(top_srcdir)/plugins/common				\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"	\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	$(AM_CPPFLAGS)

gsd_account_CFLAGS =		\
	$(PLUGIN_CFLAGS)		\
	$(ACCOUNT_CFLAGS)

gsd_account_LDADD =					\
	$(top_builddir)/gnome-settings-daemon/libgsd.la		\
	$(top_builddir)/plugins/common/libcommon.la		\
	$(ACCOUNT_LIBS)

desktopdir = $(sysconfdir)/xdg/autostart
desktop_in_files = org.gnome.SettingsDaemon.Account.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

org.gnome.SettingsDaemon.Account.desktop: $(desktop_in_files) Makefile
	$(AM_V_GEN) sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST = $(desktop_in_files)

CLEANFILES = $(desktop_DATA)

DISTCLEANFILES = $(desktop_DATA)
