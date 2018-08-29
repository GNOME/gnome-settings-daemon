FROM ppc64le/fedora:rawhide
MAINTAINER Claudio André (c) 2018 V1.0

LABEL architecture="ppc64le"
LABEL version="1.0"
LABEL description="Docker image to run CI for GNOME Settings Daemon."

ADD x86_64_qemu-ppc64le-static.tar.gz /usr/bin

RUN dnf -y upgrade && \
    dnf -y install --setopt=install_weak_deps=False \
                   @c-development git lcov gcovr clang libasan libubsan libtsan compiler-rt \
                   alsa-lib-devel colord-devel cups-devel fontconfig-devel geoclue2-devel geocode-glib-devel \
                   gettext git glib2-devel gnome-desktop3-devel gnome-session gobject-introspection \
                   gsettings-desktop-schemas-devel gtk3-devel lcms2-devel libcanberra-devel libgtop2-devel \
                   libgudev-devel libgweather-devel libnotify-devel librsvg2-devel libX11-devel libXi-devel \
                   libXtst-devel libwacom-devel meson NetworkManager-libnm-devel nss-devel perl-interpreter \
                   polkit-devel pulseaudio-libs-devel pygobject3 python3-dbusmock upower-devel wayland-devel \
                   which xorg-x11-drv-wacom-devel xorg-x11-server-Xvfb xorg-x11-utils mutter \
                   mesa-dri-drivers umockdev llvm gcr-devel ModemManager-glib-devel && \
     dnf -y clean all

CMD ["/bin/bash"]

