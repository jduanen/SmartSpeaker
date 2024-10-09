#!/bin/bash

# install build stuff
sudo apt-get update && sudo apt-get install -y \
    build-essential \
    wget \
    curl \
    git \
    cmake \
    meson

# install glib-2
sudo apt install libglib2.0-dev

# pkg-config --cflags --libs glib-2.0

# get sources
git clone git@github.com:jduanen/SmartSpeaker.git

# build application
cd SmartSpeaker
meson setup -Dprefix=/usr/lib/arm-linux-gnueabihf -Dlibdir=lib build

ninja -C build
ninja -C build install
