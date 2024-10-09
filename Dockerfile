FROM ubuntu:18.04

# Install necessary tools and ARM cross-compiler
RUN apt-get update && apt-get install -y \
    build-essential \
    gcc-arm-linux-gnueabihf \
    g++-arm-linux-gnueabihf \
    wget \
    curl \
    git

# Install python3
RUN apt-get update && apt-get install -y \
    python3 \
    python3-pip \
    python3-venv

# Install build tools
RUN apt-get update && apt-get install -y \
    cmake \
    pkg-config \
    ninja-build

# Install the necessary build dependencies for glib
RUN apt-get update && apt-get install -y \
    autoconf \
    automake \
    libtool \
    libmount-dev \
    gettext \
    zlib1g-dev

# Copy your project files (done before the continer is run)
COPY . /app
ENV LD_LIBRARY_PATH="/opt/arm-linux-gnueabihf/lib/"

# Cross-compile and install the required dependencies for glib
#WORKDIR /app/lib/
#RUN wget https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.17.tar.gz
#RUN tar xzf libiconv-1.17.tar.gz
#WORKDIR /app/lib/libiconv-1.17/
#RUN ./configure --host=arm-linux-gnueabihf --prefix=/opt/arm-linux-gnueabihf && \
#    make && \
#    make install
#WORKDIR /app/

#WORKDIR /app/lib/
#RUN wget https://github.com/libffi/libffi/releases/download/v3.4.4/libffi-3.4.4.tar.gz
#RUN tar xzf libffi-3.4.4.tar.gz
#WORKDIR /app/lib/libffi-3.4.4/
#RUN ./configure --host=arm-linux-gnueabihf --prefix=/opt/arm-linux-gnueabihf && \
#    make && \
#    make install

# Create a venv and install meson in it
RUN python3 -m venv /opt/venv
ENV PATH="/opt/venv/bin:${PATH}"
RUN pip3 install wheel
RUN pip3 install meson

# Cross-compile glib-2
WORKDIR /app/lib/
RUN wget https://download.gnome.org/sources/glib/2.67/glib-2.67.3.tar.xz
RUN tar xf glib-2.67.3.tar.xz
WORKDIR /app/lib/glib-2.67.3/
RUN meson setup --cross-file=/app/lib/arm-linux-gnueabihf.txt \
    -Dprefix=/opt/arm-linux-gnueabihf \
    -Dlibdir=lib \
    builddir
#RUN ninja -C builddir
##RUN ninja -C builddir install
##WORKDIR /app/

# Set up environment variables for cross-compilation
ENV CROSS_COMPILE=arm-linux-gnueabihf-
ENV CC=${CROSS_COMPILE}gcc
ENV CXX=${CROSS_COMPILE}g++
ENV STRIP=${CROSS_COMPILE}strip

# Set the default target architecture
ENV ARCH=arm

# Get QEMU to run compiled programs
##RUN dpkg --add-architecture armhf && \
##    echo "deb [arch=armhf] http://ports.ubuntu.com/ bionic main restricted universe multiverse" >> /etc/apt/sources.list && \
##    echo "deb [arch=armhf] http://ports.ubuntu.com/ bionic-updates main restricted universe multiverse" >> /etc/apt/sources.list && \
##    echo "deb [arch=armhf] http://ports.ubuntu.com/ bionic-security main restricted universe multiverse" >> /etc/apt/sources.list
RUN dpkg --add-architecture armhf
##RUN apt-get update && \
##    apt-get install -y qemu-user-static

##ENV QEMU=/usr/bin/qemu-arm-static

# build app with meson
##WORKDIR /app
##RUN meson . build --cross-file scripts/cross_file.txt
##RUN ninja -C build clean
##RUN ninja -C build

# Build command (replace with your actual build command)
##CMD ["make"]
