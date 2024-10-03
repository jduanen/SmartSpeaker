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

# Copy your project files
COPY . /app
WORKDIR /app

# Build command (replace with your actual build command)
##CMD ["make"]
