#!/bin/bash
#
# Script to build and install the smartspeaker C++ app onto a Xiaodu device

#### FIXME run 'adb devices' to get list of adb-attached devices, and pick the desired one if there's more than one
DEVICE_SN="XDHM1P1GG0324M1C7R0369971"

# build and strip the smartspeaker app
docker run --rm -v $(pwd):/app smartspeaker-cross bash -c \
  "ninja -C builddir && arm-linux-gnueabihf-strip -o builddir/src/smartspeaker.stripped builddir/src/smartspeaker"

# install on the selected 
ANDROID_SERIAL=${DEVICE_SN} scripts/install.sh builddir/src/smartspeaker.stripped

# kill the old instance and the new one will start
adb -s ${DEVICE_SN} shell 'kill $(pidof smartspeaker)'
