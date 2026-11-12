#!/bin/sh
# Run esphome in the baidu-esphome image with the armhf / GLIBC 2.23 cross
# toolchain as the host platform's compiler (flags as scripts/cross_file.txt).
# Run from baidu/. Usage: scripts/esphome.sh compile|upload|logs|run baidu-xdh-1d-a1.yaml ...
set -eu

FL="-march=armv7-a -mfloat-abi=hard -mfpu=neon --sysroot=/sysroot \
-B/sysroot/usr/lib/arm-linux-gnueabihf -L/sysroot/usr/lib/arm-linux-gnueabihf \
-L/sysroot/lib/arm-linux-gnueabihf -static-libgcc -static-libstdc++ \
-pthread -Wl,--allow-shlib-undefined"

exec docker run --rm --network host -u "$(id -u):$(id -g)" \
  -e HOME=/config/.esphome/home -v "$(pwd)":/config \
  -e CC="arm-linux-gnueabihf-gcc $FL" -e CXX="arm-linux-gnueabihf-g++ $FL" \
  -e AR=arm-linux-gnueabihf-ar -e OBJDUMP=arm-linux-gnueabihf-objdump \
  -e READELF=arm-linux-gnueabihf-readelf \
  baidu-esphome "$@"
