NULL =

enabled_plugins =	\
	a11y-keyboard	\
	a11y-settings	\
	clipboard	\
	color		\
	datetime	\
	dummy		\
	power		\
	housekeeping    \
	keyboard	\
	media-keys	\
	mouse		\
	orientation	\
	screensaver-proxy \
	sharing		\
	sound		\
	xrandr		\
	xsettings	\
	$(NULL)

disabled_plugins = $(NULL)

if SMARTCARD_SUPPORT
enabled_plugins += smartcard
else
disabled_plugins += smartcard
endif

if HAVE_WACOM
enabled_plugins += wacom
else
disabled_plugins += wacom
endif

if BUILD_PRINT_NOTIFICATIONS
enabled_plugins += print-notifications
else
disabled_plugins += print-notifications
endif

if BUILD_RFKILL
enabled_plugins += rfkill
else
disabled_plugins += rfkill
endif

SUBDIRS = common $(enabled_plugins)
DIST_SUBDIRS = $(SUBDIRS) $(disabled_plugins)
