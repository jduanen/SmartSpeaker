# SmartSpeaker
Applications for Smart Speakers

## Baidu Xiaodu Audio Speaker

??TBD??

### Hardware Details

* details
  - MAC: 48:F3:F3:*.*.*
  - mfg date: 2021/08/04
  - 12V @ 1A
  - uUSB

* features
  - 12x RGB LEDs
  - 4x push buttons
  - 6x microphones
  - 4x IR emitters?
  - high-power IR emitter?
  - IR receiver?
  - Wifi antenna and connector

* Main Semiconductors
  - AD52058 (ESMT): Class D audio amp, 2x15W
  - RTL8723DS (Realtek): 802.11bgn, SDIO, BTLE
  - R328-S3 (Allwinner): mutli-microphone audio processor
    * R328-S3
      - interfaces
        * 8x PDM
        * 16x I2S
        * 3x ADC
        * 1x DAC
        * 2x TWI
        * 2x SPI
        * 4x UART
        * 8x PWM
        * USZB 2.0 OTG
        * SDIO 3.0
        * LEDC
      - VAD voice wakeup
  - AP2122 (GigaDevice): ?
  - HTR3236 (?): 36x LED PWM, I2C interface

* Mainboard Connections
  - white 4-pin connector:
    * D?
    * D?
    * GND
    * 12VDC
  - red 4-pin connector:
    * L+
    * L-
    * R+
    * R-
  - 3x Pogo pins
    * D?
    * D?
    * GND

### Software Development

* get console
  - plug in microUSB connector
  - plug in 12V PSU
  - 'adb shell'

* set up device
  - cp /data/wifi/wpa_supplicant.conf /data/wifi/wpa_supplicant.conf.orig
  - edit /data/wifi/wpa_supplicant.conf
    * network={
        scan_ssid=1
        key_mgmt=WPA-PSK
        ssid="Linksys49812"
        psk="<pwd>"
    }
  - echo "nameserver 192.168.166.1" > /data/wifi/resolve.conf
  - create directory for my executables
    * mkdir -p /opt/ss/build
    * mkdir -p /opt/ss/lib
  - select timezone
    * ???? /etc/TZ: option hostname ?/n option timezone PDT ????
    * 'TZ=PDT date'
  - enable ssh access
    * ????
  - reboot

* building apps
  - build docker image with cross-compilation tools for arm32v7
    * 'docker build -t arm32v7-cross-compiler .'
  - run shell in docker
    * 'docker run --rm -it -v $(pwd):/app arm32v7-cross-compiler /bin/bash'
  - compile in docker
    * '${CC} ./src/helloworld.c -o ./helloworld'
  - load binaries from host to the speaker
    * push libraries
      - 'adb push ./lib/ /opt/ss/'
    * push libraries
      - 'adb push ./build/helloworld /opt/ss/build/'
      - 
  - TBD
    * install qemu-arm?
      - ????
    * run arm stuff within the container
      - 'qemu-arm <bin>'
    * move files/directories from host to speaker
      - 'adb push <local> <remote>'
      - 'scp -r <local> root@<ipaddr>:<remote>'



  - run docker
    * 'docker run --rm -it -v $(pwd):/out --security-opt label=disable arm32v7-cross-compiler /bin/bash'
    * docker run --rm -v $(pwd):/app arm32v7-cross-compiler <cmd>
  - load libraries from container to the host
    * from Dockerfile
      - COPY /usr/xcc/armv7-unknown-linux-gnueabi/armv7-unknown-linux-gnueabi/sysroot/lib/libc.so.6 /out/lib/
      