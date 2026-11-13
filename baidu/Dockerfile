# ESPHome (host platform) on top of the smartspeaker-cross image, so ESPHome
# builds armhf binaries for the speaker's GLIBC 2.23 with the same sysroot.
#
# Build:  docker build --network host -f baidu/Dockerfile -t baidu-esphome baidu
# Compile: docker run --rm -v $(pwd)/baidu:/config baidu-esphome compile <yaml>
FROM smartspeaker-cross

# ESPHome 2026.10 needs Python >= 3.12; the base image has 3.10
ENV UV_PYTHON_INSTALL_DIR=/opt/uv-python
RUN /opt/venv/bin/pip install uv==0.9.5 && \
    /opt/venv/bin/uv venv --python 3.12 /opt/esphome && \
    /opt/venv/bin/uv pip install --python /opt/esphome/bin/python esphome==2026.10.2

# ESPHome's host md5/sha256 use OpenSSL 1.1's EVP API. The device has
# libcrypto.so.1.1 (1.1.0i, symbols versioned OPENSSL_1_1_0); the xenial sysroot
# has none, so add Debian stretch's 1.1.0 armhf headers and library to it.
RUN cd /tmp && \
    for d in libssl-dev libssl1.1; do \
      python3 -c "import urllib.request,sys; urllib.request.urlretrieve(sys.argv[1], 'p.deb')" \
        "http://archive.debian.org/debian/pool/main/o/openssl/${d}_1.1.0l-1~deb9u1_armhf.deb" && \
      dpkg-deb -x p.deb /sysroot && rm p.deb; \
    done && \
    ln -sf /sysroot/usr/include/arm-linux-gnueabihf/openssl/opensslconf.h /sysroot/usr/include/openssl/opensslconf.h 2>/dev/null; \
    ls /sysroot/usr/lib/arm-linux-gnueabihf/libcrypto.so* /sysroot/usr/include/openssl/evp.h

# micro_wake_word on host: TFLite Micro (the esp-tflite-micro version ESPHome
# pins, reference kernels) and the microWakeWord frontend, as armhf static
# libraries in /opt/baidu-tp (see third_party/build.sh)
COPY third_party /tmp/tp-src
RUN /tmp/tp-src/build.sh /tmp/tp-src /opt/baidu-tp && rm -rf /tmp/tp-src

ENV PATH=/opt/esphome/bin:$PATH
WORKDIR /config
ENTRYPOINT ["esphome"]
