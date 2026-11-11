# Creating a Home Assistant Voice Satellite with Old Baidu Xiaodu Smart Speakers

I have a number of Baidu Xiaodu (XDH-1D-A1) smart speakers that I'd like to modify in order to serve as a Home Assistant Voice Satellites.
This project involves reverse engineering the devices, creating a cross-compilation environment, and building the code needed to implement the features of existing HA Voice Satellites (see ~/Code/SensorNet/voiceAssistants/ -- FIXME).


## Status Checklist (updated as work proceeds)

- [x] Phase 0a Document plan
- [ ] Phase 0 Probe stock unit, backup, install/uninstall scripts, config fix
  - [x] scripts: `probe.sh`, `backup.sh`, `install.sh`, `uninstall.sh`, `dcslaunch.sh`
  - [x] `Config::load()` reads `[audio]` and `[esphome]`
  - [x] README: stock-unit install/uninstall and WiFi
  - [ ] run probe on a unit → `notes/stock.txt`; set `OTA_DISABLE` in `install.sh`
  - [ ] run backup; install; verify on device (needs the Phase 1 strip for a stock-runnable binary)
- [ ] Phase 1 Strip and re-base build
- [ ] Phase 2 ESPHome API core
- [ ] Phase 3 Audio engine (ALSA mixer, decoders, AEC)
- [ ] Phase 4 Voice pipeline
- [ ] Phase 5 microWakeWord
- [ ] Phase 6 Media player, ducking, volume
- [ ] Phase 7 LEDs, buttons, mute, sounds
- [ ] Phase 8 Encryption, mDNS, launcher


## Context
`~/Code/SensorNet/voiceAssistants/` (FIXME ref) holds the code for four different types of ESPHome/ESP32-S3 voice satellites (HA Voice PE, ReSpeaker XVF3800, Satellite1, Waveshare). They all share the same feature set:
- server-side STT, intent and TTS over the ESPHome native API
- on-device microWakeWord (okay_nabu, hey_jarvis, plus a "stop" word)
- music ducking during speech input/output
- mic mute (hardware and software)
- an HA media_player for Music Assistant and announcements
- an LED ring driven by a phase state machine
- multi-click buttons that fire HA events
- timers

The goal of this project is to provide the same feature set on stock (i.e., unmodified) Baidu Xiaodu XDH-1D-A1 smart speakers (meaning units still running DuerOS from `/opt/duer`, without Genie installed).

### Baidu Xiaodu XDH-1D-A1 Smart Speaker System Overview
- Allwinner R328 SOC, with 2× Cortex-A7 armhf, and 128 MB RAM
- running Tina Linux 3.0.8, kernel 4.9.118, glibc 2.23, BusyBox, no Python
- 12x RGB LEDs on an I²C driver (exposed through sysfs)
- 4x ADC-ladder keys: VOLUMEUP, VOLUMEDOWN, MUTE, PLAYPAUSE
- internal codec (`hw:audiocodec`), 3 analog mics
- speaker enclosure with AD52058 amplifier driving speakers
- external power supply

### Decisions Made
- ESPHome native API, implemented in C++
- use docker-based cross-compilation tools
- microWakeWord on the device
- HA media_player URLs for music
- target is the stock firmware
- probe device first to determine what exists on the system out of the box

### What "Stock" Means in this Project
A Stanford project named Genie used these devices for a similar application and the code is still available (<ref>).

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
- **Drop PulseAudio and GStreamer:** Use direct ALSA plus an in-process mixer, the same model as ESPHome's `mixer` speaker. Owning the mixer also hands us the playback reference that echo cancellation needs.
- **Audio decoding:** vendored header-only `dr_flac` and `dr_mp3`, plus WAV. HA's ESPHome integration transcodes TTS and Music Assistant media through its ffmpeg proxy into whatever `supported_formats` the media_player advertises. Advertise FLAC at 48 kHz stereo, and also 16 kHz mono for announcements.
- **HTTP:** the device's libcurl
- **AEC and noise suppression:** the device's `speexdsp` (MDF echo canceller plus preprocess), using the mixer's output as the reference. Voice Activity Detection (VAD) is done in HA, so remove the device-side webrtc VAD.
- **Buttons:** read `/dev/input/eventN` `struct input_event` directly, dropping libevdev
- **Vendor-provided code:**
  - nanopb (light-weight protobuf)
  - tflite-micro plus microfrontend (local wake word detection)
  - Noise for encryption: `noise-c`, or `Monocypher` plus a small SHA-256 implementation
- **Linking:** link `-static-libstdc++` as today. The only dynamic dependencies are libc, libasound, libglib/gio, libcurl, libspeexdsp and libm. Check with `objdump -p` that every GLIBC version is ≤ 2.23.

The stock sysroot in `Dockerfile.dockcross` (Ubuntu xenial armhf) already supplies older-or-equal headers for all of these. Remove the pulse, gstreamer, soup, json-glib, webrtc and libevdev deps from `src/meson.build`.

Behavioral spec: **Satellite1** `~/Code/SensorNet/voiceAssistants/Satellite1/packages/common/{voice_assistant,media_player,buttons,led_ring}.yaml`. Secondary reference: HA Voice PE `HomeAssistantVoicePE/packages/home-assistant-voice.yaml`.

## Architecture (one process, `smartspeaker`, GLib main loop + 2 RT audio threads)
```
ALSA capture hw:audiocodec (48k) ─► resample 16k mono ─► speex AEC (ref = mixer out) + NS
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

### ???? Stock (DuerOS) units: probe, back up, install, uninstall

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
5. `scripts/uninstall.sh [--purge]` restores the original launcher and the disabled paths. `--purge` also removes `/opt/ss`.

WiFi is configured the same way as on Genie units, in `/data/wifi/wpa_supplicant.conf` (see "set up device" below). The probe records that file with the PSK redacted, to confirm that stock units use the same path.

`config.ini` sections: `[audio]` (backend, output/input device, volume_control, ec), `[esphome]` (name, port, encryption_key, model_dir, sound_dir), `[buttons]`, `II
**0. Probe a stock unit and take it over safely** (the user runs these over `adb shell`; record the results in `notes/stock.txt`)
- Inventory:
  - `ps w`
  - `cat /opt/duer/dcslaunch.sh`
  - `ls -lR /opt/duer | head`
  - `df`
  - `free`
  - `ls /etc/init.d /etc/rc.d`
  - `cat /proc/asound/cards /proc/asound/pcm`
  - `amixer -D hw:audiocodec contents`
  - `cat /proc/bus/input/devices`
  - `ls /sys/class/leds/`
  - `ls /usr/lib | grep -E 'curl|speex|glib|gio|asound|ssl|stdc'`
  - `fw_printenv`
- Goals of the probe:
  - Identify the DuerOS processes and launch chain, and any OTA updater or watchdog.
  - Find the LED sysfs node (`aw21036_led` or `isled01`) and whether DuerOS holds it in kernel mode (`user_space`).
  - Identify the key `eventN` device.
  - Check whether capture has a hardware loopback/reference channel, which some sunxi codecs expose. If it exists, use it as the AEC reference.
  - Measure `/opt` free space.
- **Backup before change:** `adb pull /opt/duer` and `dd` `/dev/by-name/app` to the host, so the unit can be restored.
- **Takeover script** `scripts/install.sh` (runs from the host over adb; reversible):
  - Rename `/opt/duer/dcslaunch.sh` to `.orig` and install our launcher.
  - Disable the DuerOS OTA updater found by the probe, so it can't overwrite `/opt`.
  - Create `/opt/ss/{bin,share/models,share/sounds,etc}` and push the files.
  - Also write `scripts/uninstall.sh` to restore the `.orig` launcher.
- **WiFi:** same as today (`/data/wifi/wpa_supplicant.conf`). Document it for stock units in the README.
- Fix the existing bug in `src/config.cpp`: `Config::load()` ignores everything except `[buttons]` and `[leds]`. Add `[audio]` and `[esphome]` (name, port, encryption key, model and sound dirs).
- Verify: on a stock unit with DuerOS stopped, a minimal build (LEDs plus keys plus ALSA tone playback) runs from `/opt/ss` with `ldd` resolving only to `/lib` and `/usr/lib`.

**1. Strip and re-base the build**
- Remove from the build:
  - `src/audio/pulseaudio/*`
  - the Porcupine path in `src/audio/wakeword.cpp`
  - `src/webrtc_vad_compat.c`
  - the `stt`, `spotifyd` and `ws-protocol` stubs
  - the GStreamer `AudioPlayer` internals
  - the libevdev usage in `src/evinput.cpp`
- Keep `src/glibc_compat.c`, `src/leds.cpp`, `src/audio/alsa/{input,volume,audiofifo}.cpp` and `src/app.cpp`'s main loop.
- Update `src/meson.build` deps to: glib-2.0, gio-2.0, alsa, libcurl, speexdsp, threads, m. Add `third_party/` subprojects.
- Verify: the cross-build's `objdump -p` lists only libraries present on the stock rootfs, with GLIBC ≤ 2.23. The x86_64 host build also works, for development.

**2. ESPHome API core** (new `src/esphome/`)
- Vendor `api.proto` and `api_options.proto` from aioesphomeapi and generate nanopb.
- `frame.cpp`: plaintext framing (0x00, varint length, varint type) over GIO `GSocketService` on port 6053.
- `server.cpp` handles:
  - Hello and Connect
  - DeviceInfo, with `voice_assistant_feature_flags` = VOICE_ASSISTANT | API_AUDIO | TIMERS | ANNOUNCE | START_CONVERSATION, plus MAC and name
  - ListEntities, then Done
  - SubscribeStates
  - Ping and Disconnect
  - unknown message types: log them and ignore
- `entities.cpp`: the entity registry and state pushes.
- Verify on the x86 host build: add it in HA through the ESPHome integration by IP. The entities should appear and the mute switch should toggle.

**3. Audio engine** (new `src/audio/{mixer,source,decoder,aec}.cpp`; the existing ALSA input is reused)
- **Playback:**
  - one ALSA PCM at 48 kHz stereo s16 with a period of about 20 ms
  - mixer thread sums the media and announce sources, applies the duck gain with a ramp, then master volume
- **Sources:** each runs a libcurl write callback into a decoder (dr_flac/dr_mp3/wav), resamples if needed, and fills a ring buffer. Each has states idle, playing, paused and finished.
- **Capture:** ALSA at 48k, downsampled to 16k mono, then speex echo cancellation (reference = the mixer output delayed by the measured latency), then speex noise suppression, then fanned out to the wake word and the session.
- Set amixer defaults at startup (`LINEOUT volume`, `MIC1/2 gain`), as Genie's launcher did.
- Verify:
  - play a FLAC URL from the HA media folder
  - echo test: while music plays, record the AEC output to a WAV and confirm the music is strongly attenuated
  - CPU under about 20% of a core

**4. Voice pipeline** (`src/voice/session.cpp`)
- Start the session:
  - send `VoiceAssistantRequest(start, wake_word_phrase, flags=USE_VAD)`
  - receive `VoiceAssistantResponse(port=0)`
  - stream `VoiceAssistantAudio`
- Map events to phases:
  - RUN_START, then STT_VAD_START: listening
  - STT_VAD_END: thinking
  - TTS_START/TTS_END(url): replying
  - RUN_END or ERROR: idle or error
- Play TTS on the announce source. Honor `continue_conversation`.
- Also handle:
  - `VoiceAssistantAnnounceRequest`, then `AnnounceFinished`
  - start_conversation
  - `VoiceAssistantConfigurationRequest` and `SetConfiguration`
  - `VoiceAssistantTimerEventResponse`: track timers and ring when one finishes
- Verify: push-to-talk via PLAYPAUSE. Ask "what time is it" and hear the reply.

**5. microWakeWord** (`src/audio/microwakeword.cpp`)
- Use tflite-micro (static) plus `microfrontend` (40 mel features, 30 ms window, 10 ms step), matching ESPHome `micro_wake_word`.
- Models and manifests from esphome/micro-wakeword-models: okay_nabu, hey_jarvis, hey_mycroft and stop. They are small, about 60 KB each.
- Use a sliding-window probability with a refractory period.
- Enable `stop` only during TTS, announcements and timer ringing.
- Verify: a host test on positive and negative WAVs. On the device, under about 25% of a core and RSS under about 25 MB.

**6. Media player, ducking, volume**
- `MediaPlayerCommandRequest` handles play_media, pause, play, stop, volume and mute. Announcements go to the announce source.
- Advertise `supported_formats` so HA and Music Assistant transcode to FLAC.
- Ducking follows the ESPHome yaml:
  - on wake or announce: -20 dB over 0 s
  - on end: 0 dB over 1 s
- Verify: Music Assistant radio to the device ducks during a voice reply and recovers.

**7. LEDs, buttons, mute, sounds** (extend `src/leds.cpp` and `src/evinput.cpp`)
- Before driving the LEDs, write `user_space=1` so DuerOS kernel effects are off.
- LED phases and priority from Satellite1 `led_ring.yaml:509-555`, on 12 LEDs:
  - no-HA: red twinkle
  - init: blue twinkle
  - muted: two red LEDs
  - error: red pulse
  - listening: fast spin
  - thinking: two pulsing LEDs
  - replying: counter-clockwise spin
  - timer ring
  - volume bar
  - idle: the light entity's color
- Keys:
  - VOL±: ±5% volume, with the volume-bar LEDs.
  - MUTE: toggles mic mute (stop capture and wake word, sound, LEDs, HA state).
  - PLAYPAUSE, single press: stop the timer, else the pipeline, else the announcement, else toggle music, else start Assist.
  - PLAYPAUSE double, triple or long press: fire the HA event.
- Sounds: the HA Voice PE wake, mute and timer FLACs in `/opt/ss/share/sounds`.

**8. Encryption, discovery, launcher**
- Noise `Noise_NNpsk0_25519_ChaChaPoly_SHA256` with the key from config, falling back to plaintext.
- A minimal mDNS responder for `_esphomelib._tcp` with TXT version, mac and platform.
- `scripts/dcslaunch.sh` does: wait for WiFi, start NTP, set the amixer defaults, then run `/opt/ss/bin/smartspeaker` in a respawn loop with a log in `/tmp`.
- Verify: a fresh stock unit, after `install.sh` and a reboot, is auto-discovered by HA with encryption.

## Files
- Modify:
  - `src/app.cpp`, `include/app.hpp`
  - `src/config.cpp`, `config.ini`
  - `src/leds.cpp`, `src/evinput.cpp`
  - `src/audio/audioinput.cpp`, `src/audio/alsa/*`
  - `src/state/*` (collapse into phases)
  - `src/meson.build`
  - `Dockerfile.dockcross` (add libcurl, speexdsp and asound dev packages; drop pulse and gst)
- New:
  - `src/esphome/{frame,noise,server,entities,mdns}.cpp`
  - `src/voice/session.cpp`
  - `src/audio/{mixer,source,decoder,aec,microwakeword}.cpp`
  - `third_party/{nanopb,tflite-micro,dr_libs,noise}`
  - `proto/api.proto`
  - `scripts/{install,uninstall,dcslaunch,deploy}.sh`
  - `notes/stock.txt`
  - `test/` (frame codec, decoder, wake word on WAVs)
- Docs: update `README.md` and `DEVELOPMENT.md` with the stock-unit install and uninstall, the HA setup, entities, LED and button behavior, and config keys.

## Risks
- **DuerOS OTA updates or a watchdog** may restore `/opt/duer` or reboot the unit. The probe must find them before takeover.
- **`/opt` free space on stock is unknown.** Our footprint is about 3–5 MB (binary, models, sounds). If space is tight, the DuerOS payload can be moved off-device only after a host backup.
- **speexdsp AEC is weaker than webrtc or XMOS AEC**, and there is no beamforming across the 3 mics. Expect lower wake-word accuracy during loud music.
- **ESPHome API drift:** pin the proto version and ignore unknown messages.
- **Stock and Genie units may fit different LED chips:** make the path configurable, with auto-detect between the two sysfs nodes.

## Verification (end to end)
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

### Home Assistant Voice Remote Local Assistant

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



