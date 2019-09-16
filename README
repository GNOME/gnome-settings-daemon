Introduction to GNOME Settings Daemon
===============================================================================

The GNOME Settings Daemon is responsible for setting various parameters of a
GNOME Session and the applications that run under it.

This package is known to build and work properly using an LFS-9.0 platform.


Dependencies
-------------------------------------------------------------------------------

Required:

  * colord-1.4.4 (http://www.linuxfromscratch.org/blfs/view/systemd/general/colord.html)
  * Fontconfig-2.13.1 (http://www.linuxfromscratch.org/blfs/view/systemd/general/fontconfig.html)
  * GeoClue-2.5.3 (http://www.linuxfromscratch.org/blfs/view/systemd/basicnet/geoclue2.html)
  * geocode-glib-3.26.1 (http://www.linuxfromscratch.org/blfs/view/systemd/gnome/geocode-glib.html)
  * gnome-desktop-3.32.2 (http://www.linuxfromscratch.org/blfs/view/systemd/gnome/gnome-desktop.html)
  * Little CMS-2.9 (http://www.linuxfromscratch.org/blfs/view/systemd/general/lcms2.html)
  * libcanberra-0.30 (http://www.linuxfromscratch.org/blfs/view/systemd/multimedia/libcanberra.html)
  * libgweather-3.32.2 (http://www.linuxfromscratch.org/blfs/view/systemd/gnome/libgweather.html)
  * libnotify-0.7.8 (http://www.linuxfromscratch.org/blfs/view/systemd/x/libnotify.html)
  * librsvg-2.44.14 (http://www.linuxfromscratch.org/blfs/view/systemd/general/librsvg.html)
  * libwacom-0.29 (http://www.linuxfromscratch.org/blfs/view/systemd/general/libwacom.html)
  * PulseAudio-12.2 (http://www.linuxfromscratch.org/blfs/view/systemd/multimedia/pulseaudio.html)
  * Systemd-241 (http://www.linuxfromscratch.org/blfs/view/systemd/general/systemd.html)
  * UPower-0.99.10 (http://www.linuxfromscratch.org/blfs/view/systemd/general/upower.html)
  * Xorg Wacom Driver-0.37.0 (http://www.linuxfromscratch.org/blfs/view/systemd/x/x7driver.html#xorg-wacom-driver)

Recommended:

  * ALSA-1.1.9 (http://www.linuxfromscratch.org/blfs/view/systemd/multimedia/alsa.html)
  * Cups-2.2.12 (http://www.linuxfromscratch.org/blfs/view/systemd/pst/cups.html)
  * NetworkManager-1.20.0 (http://www.linuxfromscratch.org/blfs/view/systemd/basicnet/networkmanager.html)
  * NSS-3.45 (http://www.linuxfromscratch.org/blfs/view/systemd/postlfs/nss.html)
  * Wayland-1.17.0 (http://www.linuxfromscratch.org/blfs/view/systemd/general/wayland.html)

Optional

  * python-dbusmock (https://github.com/martinpitt/python-dbusmock)
  * umockdev (https://github.com/martinpitt/umockdev)


Build
-------------------------------------------------------------------------------

Install GNOME Settings Daemon by running the following commands:

```bash
mkdir build &&
cd    build &&

meson --prefix=/usr --sysconfdir=/etc .. &&
ninja
```

To check the results, execute: `ninja test`. Note that you must have
python-dbusmock installed in order for the tests to complete successfully. Some
tests in the "power" testsuite may fail depending on the init system in use.

Now, as the root user:
```bash
ninja install
```
