FROM ubuntu:18.04

# Install necessary tools and ARM cross-compiler
RUN apt-get update && apt-get install -y \
    build-essential \
    gcc-arm-linux-gnueabihf \
    g++-arm-linux-gnueabihf \
    wget \
    curl

# Set up environment variables for cross-compilation
ENV CROSS_COMPILE=arm-linux-gnueabihf-
ENV CC=${CROSS_COMPILE}gcc
ENV CXX=${CROSS_COMPILE}g++
ENV STRIP=${CROSS_COMPILE}strip

# Set the default target architecture
ENV ARCH=arm

# Get QEMU to run compiled programs
RUN dpkg --add-architecture armhf && \
    echo "deb [arch=armhf] http://ports.ubuntu.com/ bionic main restricted universe multiverse" >> /etc/apt/sources.list && \
    echo "deb [arch=armhf] http://ports.ubuntu.com/ bionic-updates main restricted universe multiverse" >> /etc/apt/sources.list && \
    echo "deb [arch=armhf] http://ports.ubuntu.com/ bionic-security main restricted universe multiverse" >> /etc/apt/sources.list
RUN apt-get update && \
    apt-get install -y qemu-user-static

ENV QEMU=/usr/bin/qemu-arm-static

# Copy your project files
COPY . /app
WORKDIR /app

# Build command (replace with your actual build command)
##CMD ["make"]
