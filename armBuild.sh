#!/bin/bash

# install build stuff
sudo apt-get update && sudo apt-get install -y \
    build-essential \
    wget \
    curl \
    git \
    cmake \
    meson

# N.B.
#  * libglib-2.0.so
#    - /usr/lib/libglib-2.0.so.0.5000.1 (TinaLinux)
#    - libglib-2.0.so.0.6600.8
#  * TinaLinux
#    - /lib/libc.so.6: GLIBC_2.23
#    - 

# install glib-2
cd lib
wget https://download.gnome.org/sources/glib/2.58/glib-2.58.3.tar.xz
tar xf glib-2.58.3.tar.xz
cd glib-2.58.3/
mkdir builddir
cd builddir
meson setup --default-library=static ..
ninja

adb push ./glib/libglib-2.0.a /opt/ss/lib/

# get sources
git clone git@github.com:jduanen/SmartSpeaker.git

# build application
cd SmartSpeaker
meson setup -Dprefix=/usr/lib/arm-linux-gnueabihf -Dlibdir=lib --default-library=static build

ninja -C build

adb push build/src/smartspeaker /opt/ss/bin/

adb push config.ini /opt/ss/


# N.B.
# * on target:
#   - export LD_LIBRARY_PATH=/opt/ss/libs
