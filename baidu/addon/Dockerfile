# Home Assistant add-on: the official ESPHome add-on (Device Builder) plus what
# baidu's host-platform builds need, so HA's firmware update can rebuild and
# install the Xiaodu speakers while ESP32 and other devices build as before.
#
# Added to the official image:
# - the armhf cross compiler, GCC 11 from Ubuntu 22.04 as in the C++ app's
#   Dockerfile.dockcross; baidu_host selects it for configs that use it, other
#   configs never see it. (Debian trixie's GCC 14 doesn't do: its static
#   libstdc++ needs GLIBC 2.34-2.38 symbols such as the time64 ones.)
# - /sysroot: the speaker's libraries and headers (Ubuntu xenial armhf, GLIBC
#   2.23, as the C++ app's Dockerfile.dockcross) plus Debian stretch's
#   OpenSSL 1.1.0 (the device has 1.1.0i)
# - /opt/baidu-tp: TFLite Micro and the microWakeWord frontend for armhf
#   (third_party/build.sh)
#
# The build context is the folder prepared by baidu/addon/prepare.sh.
ARG BUILD_FROM=ghcr.io/esphome/esphome-hassio:2026.10.2

# --- the speaker's sysroot, from Ubuntu xenial armhf packages ---------------
FROM ubuntu:16.04 AS sysroot
ENV DEBIAN_FRONTEND=noninteractive
# armhf comes from ports.ubuntu.com; an amd64 builder's own archive doesn't
# carry it (an arm64 builder already uses ports)
RUN if [ "$(dpkg --print-architecture)" = amd64 ]; then \
      sed -i 's|^deb http|deb [arch=amd64] http|' /etc/apt/sources.list && \
      for s in xenial xenial-updates xenial-security; do \
        echo "deb [arch=armhf] http://ports.ubuntu.com/ubuntu-ports $s main restricted universe multiverse" \
          >>/etc/apt/sources.list; \
      done; \
    fi && \
    dpkg --add-architecture armhf && apt-get update && \
    apt-get install -y --no-install-recommends \
      libc6-dev:armhf libasound2-dev:armhf libcurl4-openssl-dev:armhf libspeexdsp-dev:armhf \
      python3 && \
    rm -rf /var/lib/apt/lists/*
RUN mkdir -p /sysroot/usr/lib /sysroot/lib && \
    cp -a /lib/arm-linux-gnueabihf /sysroot/lib/ && \
    cp -a /usr/lib/arm-linux-gnueabihf /sysroot/usr/lib/ && \
    cp -a /usr/include /sysroot/usr/ && \
    cd /tmp && \
    for d in libssl-dev libssl1.1; do \
      python3 -c "import urllib.request,sys; urllib.request.urlretrieve(sys.argv[1], 'p.deb')" \
        "http://archive.debian.org/debian/pool/main/o/openssl/${d}_1.1.0l-1~deb9u1_armhf.deb" && \
      dpkg-deb -x p.deb /sysroot && rm p.deb; \
    done && \
    ln -sf /sysroot/usr/include/arm-linux-gnueabihf/openssl/opensslconf.h /sysroot/usr/include/openssl/opensslconf.h && \
    find /sysroot -type l | while read -r link; do \
      target=$(readlink "$link"); \
      case "$target" in /sysroot/*) ;; /*) ln -sfn "/sysroot$target" "$link" ;; esac; \
    done

# --- the cross compiler, packed as the files of its Ubuntu 22.04 packages ----
FROM ubuntu:22.04 AS xtc
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf binutils-arm-linux-gnueabihf && \
    dpkg -L $(dpkg-query -W -f '${Package}\n' | grep -E 'arm-linux-gnueabihf|armhf-cross') | \
      while read -r f; do if [ -f "$f" ] || [ -L "$f" ]; then echo "$f"; fi; done >/tmp/files && \
    tar -cf /xtc.tar -T /tmp/files

# --- the add-on ---------------------------------------------------------------
FROM ${BUILD_FROM}
COPY --from=xtc /xtc.tar /tmp/xtc.tar
# the compiler's own runtime libraries, under the sonames Ubuntu 22.04 uses
RUN tar -xf /tmp/xtc.tar -C / && rm /tmp/xtc.tar && \
    apt-get update && \
    apt-get install -y --no-install-recommends libgmp10 libmpfr6 libmpc3 libisl23 libzstd1 zlib1g && \
    rm -rf /var/lib/apt/lists/* && \
    missing=$(for b in /usr/bin/arm-linux-gnueabihf-* /usr/libexec/gcc-cross/arm-linux-gnueabihf/*/*; do \
      if [ -f "$b" ] && [ -x "$b" ]; then ldd "$b" 2>/dev/null | grep "not found" || true; fi; done | sort -u) && \
    if [ -n "$missing" ]; then echo "cross compiler libraries missing: $missing"; exit 1; fi && \
    arm-linux-gnueabihf-g++ --version | head -1
COPY --from=sysroot /sysroot /sysroot
COPY third_party /tmp/tp-src
RUN /tmp/tp-src/build.sh /tmp/tp-src /opt/baidu-tp && rm -rf /tmp/tp-src
