# Dockerfile to create a container that has cross-compilation tools
#  that allows the generation of executables for armhf processors
#  on an x86 platform.

ARG ARCH=arm32v7
FROM dockcross/linux-armv7

# baseline deps
RUN apt-get update && \
    apt-get -y --allow-unauthenticated install build-essential pkg-config \
        git wget libasound2-dev libglib2.0-dev \
        libjson-glib-dev libsoup2.4-dev libevdev-dev libgstreamer1.0-dev \
        python3 python3-pip python3-venv \
        libmount-dev libffi-dev libsemanage-dev \
        libogg-dev libvorbis-dev libmpg123-dev libspeex-dev libspeexdsp-dev \
        sound-theme-freedesktop \
        libwebrtc-audio-processing-dev  \
        zlib1g-dev libncurses5-dev libnss3-dev libssl-dev \
        libreadline-dev libbz2-dev

RUN python3 -m venv /opt/venv
ENV PATH="/opt/venv/bin:${PATH}"

RUN mkdir -p /src/scripts /out
WORKDIR /src

# ?
#COPY cli/requirements.txt /src/cli/requirements.txt
#RUN python3 -m pip install -r /src/cli/requirements.txt

# get puls
RUN apt-get update && apt-get -y install pulseaudio

RUN apt-get -y install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio

