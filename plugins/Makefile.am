NULL =

enabled_plugins =	\
	a11y-keyboard	\
	background	\
	clipboard	\
	datetime	\
	dummy		\
	housekeeping    \
	keybindings	\
	keyboard	\
	media-keys	\
	mouse		\
	sound		\
	xrandr		\
	xrdb		\
	xsettings	\
	$(NULL)

disabled_plugins = $(NULL)

if BUILD_GCONF_BRIDGE
enabled_plugins += gconf
else
disabled_plugins += gconf
fi

if SMARTCARD_SUPPORT
enabled_plugins += smartcard
else
disabled_plugins += smartcard
endif

SUBDIRS = common $(enabled_plugins)
DIST_SUBDIRS = $(SUBDIRS) $(disabled_plugins)
