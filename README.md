# SmartSpeaker
Applications for Smart Speakers

## Baidu Xiaodu Audio Speaker

??TBD??

### Hardware Details

* Device
  - Baidu Xiaodu
  - XDH-1D-A1

* OS: see 'osbuild.txt'

* HW details on the box
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
  - 128MB flash storage
    * cat /sys/nand_driver0
      - Model: GD5F1GQ5UEYIG
      - NandID: 0xffff51c8

* Main Semiconductors
  - AD52058 (ESMT): Class D audio amp, 2x15W
  - RTL8723DS (Realtek): 802.11bgn, SDIO, BT 4.2
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

# Home Assistant Voice Remote Local Assistant

* Assist Pipeline
  - all local processing
  - Speech To Text (STT)
    * Whisper
      - faster-whisper fork
      - RasPi4: ~8secs; Intel NUC: <1sec
      - CUDA acceleration is available (but haven't been able to get it to work)
  - Text To Speech (TTS)
    * Piper
      - optimized for RasPi4
      - supports multiple languages
      - RasPi4 (medium quality model): 1.6 secs of voice in one second
  - Wake Word Detection
    * openWakeWord
  - Whisper and Piper add-ons to HA (run in containers on the HA Server)
    * also openWakeWord add-on
    * Wyoming Integration: discovers services using Wyoming protocol
      - Wyoming protocol is used to communicate between the services

* Integration with HA
  - Settings->Voice assistants->Add assistant
    * Configuration
    * Conversation Agent
    * Speech-to-Text
    * Text-to-Speech
    * Wake Word
  - expose your devices to Assist
  - Fine-tuning Whisper and Piper
    * Goto Whisper and Piper add-ons and open Documentation pages
  - custom integration
    * run TTS and STT on x86 machine with GPU (gpuServer1)
      - there are python bindings for Piper and Whisper
    * use HA's RESTful API or MQTT to communicate between server and HA
    * create custom components or scripts in HA to send text for TTS and receive audio for STT
      - e.g., send TTS requests from HA to server for audio output
      - stream audio from HA to server for STT
      - return transcribed text back to HA for further actions

* Run Whisper and Piper on Server
  - options set in "docker-compose.yml"
  - run service with docker compose
    * start: 'docker compose up -d'
      - stays up until stopped (in yml file)
    * stop and remove containers: 'docker compose down'
    * list containers: 'docker compose ps'
    * list images used in containers: 'docker compose images'
    * kill containers: 'docker compose kill'
    * remove containers: 'docker compose rm'
    * get logs: 'docker compose logs'
    * pause/unpause: 'docker compose pause/unpause'
    * show resource use in realtime: 'docker compose stats'



