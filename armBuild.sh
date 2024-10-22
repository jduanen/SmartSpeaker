#!/bin/bash

# install build stuff
sudo apt-get update && sudo apt-get install -y \
    build-essential \
    cmake \
    meson \
    libffi-dev \
    gettext \
    libmount libmount-dev \
    libglib2.0-dev \
    adb

mkdir ~/Code
cd ~/Code
