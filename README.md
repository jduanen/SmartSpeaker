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
  - 128MB DRAM
  - ?~256MB storage?

* Main Semiconductors
  - AD52058 (ESMT): Class D audio amp, 2x15W
  - RTL8723DS (Realtek): 802.11bgn, SDIO, BTLE
  - R328-S3 (Allwinner): mutli-microphone audio processor
    * CPUs
      - dual-core ARMv7, BogoMIPS: 57.14
    * IO
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
  - connect device to host with microUSB connector
  - plug device into 12V PSU
  - on host: 'adb shell'

* set up device
  - cp /data/wifi/wpa_supplicant.conf /data/wifi/wpa_supplicant.conf.orig
  - edit /data/wifi/wpa_supplicant.conf
    * network={
        scan_ssid=1
        key_mgmt=WPA-PSK
        ssid="<ssid>"
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
