# Creating a Home Assistant Voice Satellite with Old Baidu Xiaodu Smart Speakers

I have a number of Baidu Xiaodu (XDH-1D-A1) smart speakers that I'd like to modify in order to serve as a Home Assistant Voice Satellites.

The goal of this project is to provide the same feature set on both stock (i.e., unmodified) Baidu Xiaodu XDH-1D-A1 smart speakers (meaning units still running DuerOS from `/opt/duer`), and those with the Stanford Genie software installed.

This project involves the reverse engineering the smart speaker, creating a cross-compilation environment, and building the code needed to implement the
features of existing HA Voice Satellites (see ~/Code/SensorNet/voiceAssistants/ -- FIXME).

## Context

### Baidu Xiaodu XDH-1D-A1 Smart Speaker System Overview

- Allwinner R328 SOC, with 2× Cortex-A7 armhf, and 128 MB RAM
- running Tina Linux 3.0.8, kernel 4.9.118, glibc 2.23, BusyBox, no Python
- 12x RGB LEDs on an I²C driver (exposed through sysfs)
- 4x ADC-ladder keys: VOLUMEUP, VOLUMEDOWN, MUTE, PLAYPAUSE
- internal codec (`hw:audiocodec`), 3 analog mics
- speaker enclosure with AD52058 amplifier driving speakers
- external power supply

### Home Assistant Voice Satellites
`~/Code/SensorNet/voiceAssistants/` (FIXME ref) holds the code for four different types of ESPHome/ESP32-S3 voice satellites (HA Voice PE, ReSpeaker XVF3800, Satellite1, Waveshare). They all share the same feature set:
- server-side STT, intent and TTS over the ESPHome native API
- on-device microWakeWord (okay_nabu, hey_jarvis, plus a "stop" word)
- music ducking during speech input/output
- mic mute (hardware and software)
- an HA media_player for Music Assistant and announcements
- an LED ring driven by a phase state machine
- multi-click buttons that fire HA events
- timers

### Starting Assumptions
- Implement the application using the ESPHome native API, implemented in C++
- Use docker-based cross-compilation tools for the build on a linux server
- Use microWakeWord on the device for wake word detection
- Implement the HA media_player URLs for music
- Target both stock firmware and units with Genie installed
- Probe a device first to determine what exists on the system out of the box

### What "Stock" Means in this Project
A Stanford project named Genie used these devices for a similar application and the code was available (https://wiki.genie.stanford.edu/getting-started/installation-guide/xiaodu-speaker).

An attempt was made to create an app in this repo that was derived from Genie (using C++14/GLib) that depends on libraries that only exist in Genie's `/opt/genie/lib` -- e.g., PulseAudio 15, GStreamer, libsoup, json-glib, webrtc-audio-processing, libevdev and Porcupine.

It was determined that this was too much and not a good starting point, so this approach was abandoned in favor of the one documented here.

The stock rootfs (`TinaLinux.txt` opkg list) provides only:
- alsa-lib 1.1.4 and alsa-plugins-aw
- glib2 2.50.1 (including gio)
- libcurl 7.54.1 with OpenSSL 1.1.0i
- libspeexdsp 1.2rc3
- libjson-c 0.12
- libstdcpp

So the binary must run on those libraries alone. Everything else is provided as small static C/C++ sources:
- **Drop PulseAudio and GStreamer:** Use direct ALSA plus an in-process mixer, the same model as ESPHome's `mixer` speaker. The echo-cancellation reference comes from the hardware (see AEC below).
- **Audio decoding:** vendored header-only `dr_flac` and `dr_mp3`, plus WAV. HA's ESPHome integration transcodes TTS and Music Assistant media through its ffmpeg proxy into whatever `supported_formats` the media_player advertises. Advertise FLAC at 48 kHz stereo, and also 16 kHz mono for announcements.
- **HTTP:** the device's libcurl
- **AEC and noise suppression:** the device's `speexdsp` (MDF echo canceller plus preprocess), using capture channel 3 as the reference. The board wires the speaker signal into the codec's MIC3/Xadc input, so the reference is sample-synchronous with the mics and needs no delay estimation (measured in `notes/stock.txt`). Voice Activity Detection (VAD) is done in HA, so remove the device-side webrtc VAD.
- **Buttons:** read `/dev/input/eventN` `struct input_event` directly, dropping libevdev
- **Vendor-provided code:**
  - nanopb (light-weight protobuf)
  - tflite-micro plus microfrontend (local wake word detection)
  - Noise for encryption: `noise-c`, or `Monocypher` plus a small SHA-256 implementation
- **Linking:** link `-static-libstdc++` as today. The only dynamic dependencies are libc, libasound, libglib/gio, libcurl, libspeexdsp and libm. Check with `objdump -p` that every GLIBC version is ≤ 2.23.

The stock sysroot in `Dockerfile.dockcross` (Ubuntu xenial armhf) already supplies older-or-equal headers for all of these. Remove the pulse, gstreamer, soup, json-glib, webrtc and libevdev deps from `src/meson.build`.

Behavioral spec: **Satellite1** `~/Code/SensorNet/voiceAssistants/Satellite1/packages/common/{voice_assistant,media_player,buttons,led_ring}.yaml`. Secondary reference: HA Voice PE `HomeAssistantVoicePE/packages/home-assistant-voice.yaml`.

## Functional Specifications

The desired behavior is both that of a fully-featured Home Assistant Satellite
Voice Assistant as well as a Media Player.

### Buttons
- **Microphone Disable:** turns off the mic. the device doesn't respond to wakewords or the PLAYPAUSE button, but otherwise continues with all audio output functions
- **+/-:** turns speaker volume up/down by %5 per click. holding them down auto-repeats
- **PLAYPAUSE:** (Voice PE timings: presses < 1 s, gaps < 0.25 s, hold 1 s)
  * single click: stop the ring, else the pipeline, else the reply/announcement, else pause music, else start the Assist function
  * double/triple/long click: fire the HA "Button press" event with its Voice PE sounds

### LED Ring
The 12x LEDs' phases and priority are modelled after those of the other Satellites.

- LED Ring states:
  * No-HA: red twinkle
  * Init: blue twinkle
  * Muted: two red LEDs
  * Error: red pulse
  * Listening: fast spin
  * Thinking: two pulsing LEDs
  * Replying: counter-clockwise spin
  * Timer Ring: when timer expires
  * Volume Bar: when adjusting volume
  * Idle: The HA light entity's color

## Architecture (one process, `smartspeaker`, GLib main loop + 2 RT audio threads)
```
ALSA capture hw:audiocodec (48k, 3 ch) ─► DC block, resample 16k: mics→mono, ch3→ref ─► speex AEC + NS
   ├─► MicroWakeWord (tflite-micro + microfrontend) ─wake─► VoiceSession
   └─► (session active) VoiceAssistantAudio frames ─► HA
ESPHome API server :6053 (nanopb; plaintext, then Noise)  + tiny mDNS responder
   ◄── VoiceAssistantEvent (phases, TTS URL), MediaPlayerCommand, Announce, Timer events
Sources (libcurl fetch → dr_flac/dr_mp3/wav decode thread → ring buffer):
   "media" ──► duck gain ─┐
   "announce" ────────────┴─► Mixer ─► master volume ─► ALSA playback hw:audiocodec
Leds (sysfs set_all_led_color, ~25 fps timer)   Keys (/dev/input/eventN)
```

Entities exposed to HA:
- **media_player**: supports announcements, volume and mute
- **switch** "Mute microphone"
- **select** for wake word, and **select** for sensitivity
- **light** "LED ring"
- **event** "Button press"
- **switch** "Wake sound"

## Development Phases
See [DEVELOPMENT.md](DEVELOPMENT.md)

## Issues
- **`/opt` free space on stock is tight.** Our footprint is about 3–5 MB (binary, models, sounds). If space is tight, the DuerOS payload can be moved off-device only after a host backup. Already moved Genie off.
- **speexdsp AEC is weaker than webrtc or XMOS AEC**, and there is no beamforming across the 3 mics. Expect lower wake-word accuracy during loud music. Need to explore alternatives.
- **ESPHome API drift:** pin the proto version and ignore unknown messages. Try to fix by making it a proper ESPHome device.
- **Stock and Genie units may fit different LED chips:** make the path configurable, with auto-detect between the two sysfs nodes.

## TBD
- **Change from Mono to Stereo:** assumed that there is only one speaker, but there are two, so need to change the audio pipeline to do stereo
- **Improve AEC:** look into using webrtc or XMOS AEC to improve performance
- **Make it a Proper ESPHome Device:** programmable by way of yaml config file -- keep up with HA API changes, allow modifications without reprogramming, HA-based OTA updates, etc.
- **Look at improving STT/TTS performance:** look at the end-to-end flow and see where the time is being spent to see if we can reduce latency between speech input and responses

## End-to-End Verification
1. Host x86 build: HA adds the device and push-to-talk works with the laptop mic and speaker. Run `meson test -C builddir`.
2. A stock unit after `install.sh`:
   - `ldd` is clean.
   - "Okay Nabu, turn on the kitchen light" works.
   - Music Assistant playback ducks during replies.
   - "Stop" interrupts TTS.
   - The MUTE key gives red LEDs and the HA switch shows muted.
   - Volume keys sync with the media_player.
   - A double press fires the HA event.
   - A timer rings and the key stops it.
   - The unit survives a reboot.
   - `uninstall.sh` restores DuerOS.


## Baidu Xiaodu Audio Speaker Notes

The following are notes that I gathered during the process of reverse-
engineering the smart speaker.

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
* Key Features
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

### Stock (DuerOS) units: probe, back up, install, uninstall

Run these from the repo root on the host, with the unit connected over adb:

1. `scripts/probe.sh` is read-only on the device. It writes an inventory to `notes/stock.txt`.
2. `scripts/backup.sh` saves `/opt/duer`, `/data/wifi`, `fw_printenv` and md5-checked images of the `env` and `app` partitions into `backups/<serial>/`. That directory is gitignored because it holds the WiFi PSK.
3. Set `OTA_DISABLE` in `scripts/install.sh` to the DuerOS updater and watchdog paths that the probe found.
4. `scripts/install.sh [binary]` refuses to run without a backup. It then:
   - pushes the binary to `/opt/ss/bin`
   - pushes `config.ini` to `/opt/ss/etc`, keeping any config already on the device
   - pushes models and sounds to `/opt/ss/share`
   - renames `/opt/duer/dcslaunch.sh` to `.orig` and installs `scripts/dcslaunch.sh` in its place
   - renames the `OTA_DISABLE` paths to `.orig`

   Then `adb reboot`. The log is in `/tmp/smartspeaker.log`.

   On a Genie unit, `/opt` is ~97% full, so either install a stripped binary (`arm-linux-gnueabihf-strip -o builddir/src/smartspeaker.stripped builddir/src/smartspeaker` in the cross image, then `scripts/install.sh builddir/src/smartspeaker.stripped`; ~330 KB instead of 5 MB) or remove `/opt/genie` first. Once `/opt/genie` is gone, the `.orig` launcher no longer works; to go back to Genie, restore `backups/<serial>/app.img` to the `app` partition.

   The launcher also starts `scripts/wifi_watchdog.sh`. On this unit the RTL8723DS sometimes stays associated but stops receiving broadcast frames (most likely a failed WPA group-key rekey). Peers then can't ARP it, and TCP from HA stalls for 7–15 s per connect, so streams stall too. Every 60 s the watchdog makes a TCP connect to HA (the peer of the ESPHome API connection), or to the gateway until HA has connected. Any answer counts as reachable, even "refused"; only a timeout counts as a failure. After 3 timeouts in a row it runs `wpa_cli reassociate`. Tunables: `WD_INTERVAL`, `WD_FAILS`, `WD_PORT`.

   Long delays before speech (HTTP fetches that connect but then stall for 30–40 s, while the same fetch from a wired host takes 6 ms) were RF, not software: the unit sat between two APs sharing channel 11, and they kept deauthenticating it (`dmesg`: `OnDeAuth reason=34`, frames not acknowledged) every 10–40 s. Moving one AP to channel 1 and keeping the unit in the open fixed it (fetches 7–30 ms, no deauths). Driver power save (`rtw_power_mgnt=0`), RX A-MPDU and pinning a BSSID made no difference. To diagnose: `dmesg | grep -E "OnDeAuth|new candidate"` and `cat /proc/net/rtl8723ds/wlan0/rx_signal`. For one-off stalls, the announce source (TTS replies, announcements and chimes) restarts a fetch that receives nothing for 5 s, up to 2 times, skipping the bytes it already has. Music keeps a 30 s stall limit and no retry.
5. `scripts/uninstall.sh [--purge]` restores the original launcher and the disabled paths. `--purge` also removes `/opt/ss`.

WiFi is configured the same way as on Genie units, in `/data/wifi/wpa_supplicant.conf` (see "set up device" above). The probe records that file with the PSK redacted, to confirm that stock units use the same path.

`config.ini` sections: `[audio]` (output, input, volume, stereo2mono), `[ec]` (enabled, loopback), `[sound]` (wake, alarm_clock_elapsed, mute_on, mute_off, button_double, button_triple, button_long: WAV/FLAC/MP3 files in sound_dir, defaulting to the Voice PE sounds), `[esphome]` (name — empty means `xiaodu-<MAC suffix>`, port, encryption_key — base64 Noise key, empty until HA provisions one, model_dir, sound_dir), `[wakeword]` (active — a model id in model_dir; sensitivity — Slightly/Moderately/Very sensitive; both written back when changed from HA), `[buttons]`, `[leds]` (enabled, type, path; ring, ring_color, ring_brightness — the HA "LED ring" light, written back when changed there).

## Home Assistant Voice Remote Local Assistant Notes

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



