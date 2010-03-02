icondir = $(datadir)/icons/hicolor
context = apps
stock_config_icon_dir = $(datadir)/gnome-settings-daemon/xrandr

BUILT_SOURCES =				\
	gsd-xrandr-manager-glue.h

ICON_FILES =				\
	gsd-xrandr-16.png		\
	gsd-xrandr-22.png		\
	gsd-xrandr-24.png		\
	gsd-xrandr-32.png		\
	gsd-xrandr.svg

STOCK_CONFIG_ICON_FILES =		\
	gsd-xrandr-clone-laptop.png	\
	gsd-xrandr-clone-monitors.png	\
	gsd-xrandr-custom.png		\
	gsd-xrandr-extended-ontop.png	\
	gsd-xrandr-extended-right.png	\
	gsd-xrandr-external.png		\
	gsd-xrandr-laptop.png

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(icondir)/16x16/$(context)
	$(mkinstalldirs) $(DESTDIR)$(icondir)/22x22/$(context)
	$(mkinstalldirs) $(DESTDIR)$(icondir)/24x24/$(context)
	$(mkinstalldirs) $(DESTDIR)$(icondir)/32x32/$(context)
	$(mkinstalldirs) $(DESTDIR)$(icondir)/scalable/$(context)
	$(INSTALL_DATA) $(srcdir)/gsd-xrandr-16.png $(DESTDIR)$(icondir)/16x16/$(context)/gsd-xrandr.png
	$(INSTALL_DATA) $(srcdir)/gsd-xrandr-22.png $(DESTDIR)$(icondir)/22x22/$(context)/gsd-xrandr.png
	$(INSTALL_DATA) $(srcdir)/gsd-xrandr-24.png $(DESTDIR)$(icondir)/24x24/$(context)/gsd-xrandr.png
	$(INSTALL_DATA) $(srcdir)/gsd-xrandr-32.png $(DESTDIR)$(icondir)/32x32/$(context)/gsd-xrandr.png
	$(INSTALL_DATA) $(srcdir)/gsd-xrandr.svg $(DESTDIR)$(icondir)/scalable/$(context)/gsd-xrandr.svg
	$(MKDIR_P) $(stock_config_icon_dir)
	$(INSTALL_DATA) $(srcdir)/gsd-xrandr-clone-laptop.png $(stock_config_icon_dir)/gsd-xrandr-clone-laptop.png
	$(INSTALL_DATA) $(srcdir)/gsd-xrandr-clone-monitors.png $(stock_config_icon_dir)/gsd-xrandr-clone-monitors.png
	$(INSTALL_DATA) $(srcdir)/gsd-xrandr-custom.png $(stock_config_icon_dir)/gsd-xrandr-custom.png
	$(INSTALL_DATA) $(srcdir)/gsd-xrandr-extended-ontop.png $(stock_config_icon_dir)/gsd-xrandr-extended-ontop.png
	$(INSTALL_DATA) $(srcdir)/gsd-xrandr-extended-right.png $(stock_config_icon_dir)/gsd-xrandr-extended-right.png
	$(INSTALL_DATA) $(srcdir)/gsd-xrandr-external.png $(stock_config_icon_dir)/gsd-xrandr-external.png
	$(INSTALL_DATA) $(srcdir)/gsd-xrandr-laptop.png $(stock_config_icon_dir)/gsd-xrandr-laptop.png

uninstall-local:
	rm -f $(DESTDIR)$(icondir)/16x16/$(context)/gsd-xrandr.png
	rm -f $(DESTDIR)$(icondir)/22x22/$(context)/gsd-xrandr.png
	rm -f $(DESTDIR)$(icondir)/24x24/$(context)/gsd-xrandr.png
	rm -f $(DESTDIR)$(icondir)/32x32/$(context)/gsd-xrandr.png
	rm -f $(DESTDIR)$(icondir)/scalable/$(context)/gsd-xrandr.svg
	rm -f $(stock_config_icon_dir)/gsd-xrandr-clone-laptop.png
	rm -f $(stock_config_icon_dir)/gsd-xrandr-clone-monitors.png
	rm -f $(stock_config_icon_dir)/gsd-xrandr-custom.png
	rm -f $(stock_config_icon_dir)/gsd-xrandr-extended-ontop.png
	rm -f $(stock_config_icon_dir)/gsd-xrandr-extended-right.png
	rm -f $(stock_config_icon_dir)/gsd-xrandr-external.png
	rm -f $(stock_config_icon_dir)/gsd-xrandr-laptop.png
	rmdir $(stock_config_icon_dir)

plugin_LTLIBRARIES = \
	libxrandr.la

gsd-xrandr-manager-glue.h: gsd-xrandr-manager.xml Makefile
	dbus-binding-tool --prefix=gsd_xrandr_manager --mode=glib-server $< > xgen-$(@F) \
	&& ( cmp -s xgen-$(@F) $@ || cp xgen-$(@F) $@ ) \
	&& rm -f xgen-$(@F)

libxrandr_la_SOURCES = 		\
	$(BUILT_SOURCES)	\
	gsd-xrandr-plugin.h	\
	gsd-xrandr-plugin.c	\
	gsd-xrandr-manager.h	\
	gsd-xrandr-manager.c

libxrandr_la_CPPFLAGS =							\
	-I$(top_srcdir)/gnome-settings-daemon				\
	-I$(top_srcdir)/plugins/common					\
	-DBINDIR=\"$(bindir)\"						\
	-DGNOME_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"		\
	-DGNOME_SETTINGS_DATADIR=\""$(datadir)/gnome-settings-daemon"\"	\
	$(AM_CPPFLAGS)

libxrandr_la_CFLAGS =			\
	$(SETTINGS_PLUGIN_CFLAGS)	\
	$(LIBNOTIFY_CFLAGS)		\
	$(AM_CFLAGS)

libxrandr_la_LDFLAGS = 			\
	$(GSD_PLUGIN_LDFLAGS)

libxrandr_la_LIBADD  =					\
	$(top_builddir)/plugins/common/libcommon.la	\
	$(SETTINGS_PLUGIN_LIBS)				\
	$(LIBNOTIFY_LIBS)

plugin_in_files =			\
	xrandr.gnome-settings-plugin.in

plugin_DATA = $(plugin_in_files:.gnome-settings-plugin.in=.gnome-settings-plugin)

EXTRA_DIST = $(plugin_in_files) $(ICON_FILES) $(STOCK_CONFIG_ICON_FILES) gsd-xrandr-manager.xml
CLEANFILES = $(plugin_DATA) $(BUILT_SOURCES)
DISTCLEANFILES = $(plugin_DATA)

@GSD_INTLTOOL_PLUGIN_RULE@
