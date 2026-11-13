# baidu: Using the Xiaodu Smart Speaker as an ESPHome Device

This is an ESPHome (host platform) firmware for the Baidu Xiaodu XDH-1D-A1, configured by YAML and updated by ESPHome OTA. It is separate from the C++ app in the parent directory, which stays as it is; that one names itself `xiaodu-…`, this one `baidu-…`.
## Build

```bash
docker build --network host -f baidu/Dockerfile -t baidu-esphome baidu   # once
cd baidu
echo "ota_password: \"$(openssl rand -hex 16)\"" > secrets.yaml         # once; gitignored
echo "baidu_143a00_api_key: \"$(openssl rand -base64 32)\"" >> secrets.yaml  # once per speaker
scripts/esphome.sh compile baidu-143a00.yaml
scripts/esphome.sh upload baidu-143a00.yaml --device <ip>                # OTA
```

- Each speaker has a small file, `baidu-<MAC suffix>.yaml` (`baidu-143a00.yaml`, `baidu-1486a2.yaml`), that sets its `mac`, `mac_suffix` and `api_key` (`!secret baidu_<MAC suffix>_api_key`) and includes the shared `baidu-xdh-1d-a1.yaml` as a package. For a new speaker, copy one, change all three and add its key to `secrets.yaml`. The key is in the YAML, not only provisioned by HA, so that the ESPHome Device Builder's Logs can connect; HA asks for it when it adds the speaker.
- `baidu/Dockerfile` is `smartspeaker-cross` plus ESPHome 2026.10.2 (Python 3.12 via uv), Debian stretch's OpenSSL 1.1.0 armhf headers and library in `/sysroot`, and TFLite Micro and the wake word frontend in `/opt/baidu-tp`.
- `scripts/esphome.sh` runs `esphome` in that image. The cross compiler is chosen by `components/baidu_host` itself: its code generation sets `CC`, `CXX`, `AR`, `OBJDUMP` and `READELF` (which ESPHome's host build reads when it starts) to the armhf cross tools with the sysroot flags from `../scripts/cross_file.txt`, plus `-pthread` (glibc 2.23 keeps pthreads in libpthread) and `-O2` (the host build adds no optimisation flag; at `-O0` the microphone alone used 24% of a core). So only configs with `baidu_host:` are cross-compiled, which is what lets the HA add-on below build ESP32 devices as well (that add-on isn't in use; see "Managing the speakers from HA's ESPHome Device Builder").
- The program is `.esphome/build/<name>/.pioenvs/<name>/program`. After switching compilers or images, run `scripts/esphome.sh clean <file>` first: ninja keeps object files whose command line hasn't changed.
- `components/baidu_host` also carries `glibc_compat.c` (copied from `../src`, plus `pthread_cond_clockwait`, which GCC 11's libstdc++ uses for `condition_variable::wait_for` and glibc 2.23 lacks) and `host_shim.h` (FreeRTOS tick types and `esp_err_t` for host).
- `components/audio` and `components/ring_buffer` override ESPHome 2026.10.2's components of the same name so `voice_assistant` builds on host. Changes from upstream:
  - `audio/__init__.py`: allowed on `host` as well as `esp32`; `to_code` skips the ESP-IDF libraries on host.
  - `audio/audio_transfer_buffer.{h,cpp}`: compiled under `USE_HOST` too, with `host_shim.h` in place of the FreeRTOS/ESP-IDF headers, and `Speaker::play()` without the ESP32-only ticks argument. The decoder, reader and resampler stay ESP32-only.
  - `ring_buffer/__init__.py`: no `esp32` dependency. `ring_buffer.h` gains a `USE_HOST` class with the same interface, implemented in `ring_buffer_host.cpp` (byte ring, std::mutex/condition_variable, ticks are ms).
  Re-check these against upstream when changing the pinned ESPHome version.

## The device YAML

`baidu-xdh-1d-a1.yaml` behaves like the Home Assistant Voice PE: its automations follow Voice PE's `packages/home-assistant-voice.yaml` (ESPHome 2026.10), with our components in place of the ESP32 ones:
- **Wake word and stop word:** `micro_wake_word` runs all the time (`stop_after_detection: false`), so a wake word also stops a running session, a reply or a ringing timer; "stop" is enabled while a reply plays and while a timer rings. Wake word sensitivity is a select, as on Voice PE.
- **LED phases:** Voice PE's effects and `control_leds` priority, on two `partition` lights over the `baidu_leds` ring (also 12 LEDs): `voice_assistant_leds` (internal, the animations) and `led_ring` ("LED Ring" in HA, whose colour and brightness are the accent; shown when idle and on). Startup twinkles blue until HA connects, red while HA is disconnected.
- **Keys:** Play/Pause is Voice PE's centre button (single: stop the timer ring, else Assist, else the announcement, else pause the music, else start Assist; double, triple and long fire the "Button press" event with their sounds). Mute toggles the Mute switch (with the mute sounds). Volume Up/Down step the media player by 5% (repeating every 300 ms while held) and show the volume on the ring for a second.
- **Timers:** the ring counts down while a timer runs; when it finishes, the timer sound repeats until Play/Pause, the wake word or "stop", or 15 minutes.
- **Ducking:** the music drops 20 dB for announcements, Assist and a ringing timer, and comes back over 1 s.
- **Sounds:** Voice PE's, from `share/sounds` (installed to `/opt/baidu/share/sounds`), played as `file://` announcements.

Tested on 143a00 (2026-11-12): idle ring, wake word with the wake sound and LED phases, "stop" during a reply, the Mute key (sounds and red marks), the volume keys (ring bar, repeat while held), Play/Pause single/double/triple/long, and ducking of Music Assistant music under Assist all work. A 5 s timer from HA arrived as a timer event, rang when it finished, and "stop" ended it 3 s later (the repeat loop wasn't reached). HA only uses the satellite's timers when its built-in intents handle the request: with an LLM conversation agent and "Prefer handling commands locally" off, the agent replied "Timer set" without starting one, and no timer event reached the device.

Differences from Voice PE: no dial, jack, voice kit, Sendspin, improv or factory reset; LED updates on volume and mute changes come from the media player's `on_state` (Voice PE's `on_volume`/`on_mute` are speaker-media-player-only); the timer sound repeats from a script loop (no `repeat_one`); the Mute switch's restore at boot plays no sound.

## Install on a unit

Run from `baidu/`, with the unit on adb, its WiFi set up and a backup made (`scripts/setwifi.sh` and `scripts/backup.sh` from the repo root; see the top-level README's install guide). With more than one unit on adb, choose one with `ANDROID_SERIAL=<serial>` (see the top-level README's install guide, step 2; `scripts/adb_unit.sh` is a copy of the repo root's):

```bash
scripts/esphome.sh compile baidu-<unit>.yaml
scripts/install.sh .esphome/build/<name>/.pioenvs/<name>/program
# then power-cycle the unit
```

- `install.sh` puts the program, `scripts/dcslaunch.sh` and `scripts/wifi_watchdog.sh` in `/opt/baidu/bin`, and `scripts/launcher.conf` in `/opt/baidu/etc` (an existing one is kept). The program's preferences, including the API key HA provisions, are in `/opt/baidu/prefs` and survive reinstalls and OTA.
- The stock `/etc/init.d/rc.final` runs `/opt/duer/dcslaunch.sh` at boot. `install.sh` saves the launcher that is there (DuerOS's, Genie's or the C++ app's) as `/opt/baidu/etc/dcslaunch.sh.prev`, once, and replaces it with a hook that runs `/opt/baidu/bin/dcslaunch.sh`. `OTA_DISABLE` in `install.sh` works as in the C++ app's.
- The launcher does what the C++ app's does: with `legacy_rates=true` in `launcher.conf` (the default) it reloads the WiFi driver as an 802.11b-only client and restarts `wpa_supplicant` and DHCP; it waits up to 60 s for an address, starts NTP, sets the mic preamps (`MIC1/MIC2 gain volume` 3), starts the WiFi watchdog, and runs the program in a respawn loop with `ESPHOME_PREFDIR=/opt/baidu/prefs`. The log is `/tmp/baidu.log`; it is copied to `/tmp/baidu.log.1` and truncated when it passes 1 MB.
- After that, update over WiFi from this machine: `scripts/esphome.sh compile baidu-<unit>.yaml`, then `scripts/esphome.sh upload baidu-<unit>.yaml --device baidu-<unit>.local` (`upload` sends the last build; it doesn't compile). The program replaces `/opt/baidu/bin/baidu` and re-execs itself.
- `scripts/uninstall.sh [--purge]` puts the saved launcher back (or removes the hook if there was none) and restores anything `OTA_DISABLE` renamed; `--purge` also removes `/opt/baidu`. To go back to the C++ app, run `uninstall.sh` rather than the C++ app's `install.sh`, which doesn't know baidu's hook.
- HA sees the two apps as different devices (`baidu-…` and `xiaodu-…`); the one not running shows as unavailable.

## Managing the speakers from HA's ESPHome Device Builder

HA's standard ESPHome Device Builder add-on stays as it is. The speakers are built and installed from this machine (above), and their configs are also copied to HA so the builder lists them, shows them online, streams their Logs and lets the YAML be viewed. The builder can't compile them, so don't use its Install, or HA's Firmware update for a speaker: both fail with the cross compiler missing.

Its remote build server (pairing a build machine under Settings → Send builds) doesn't help here: Device Builder 1.25.0 to 1.28.1 only packs and installs ESP32, ESP8266, RP2040, nRF52 and LibreTiny builds, not host-platform ones.

The configs here are the ones to edit; copy them to HA after a change:
1. `addon/prepare.sh` (from `baidu/`) writes the configs to `.esphome/addon/esphome` (a `baidu/` folder with the shared package and the components, and `baidu-<unit>.yaml` for each speaker, pointing at `baidu/`).
2. On the HA host (Samba share or the SSH add-on), copy the contents of that folder into `/config/esphome`.
3. Add `ota_password` and each `baidu_<unit>_api_key` from `baidu/secrets.yaml` to `/config/esphome/secrets.yaml`.

Tested (2026-11-13): those configs validate in the stock builder image (`ghcr.io/esphome/esphome-hassio:2026.10.2`), and its `esphome logs` streams 143a00's log with the key from the secrets. 143a00 then got a build from this machine over WiFi (3.5 s) and HA reconnected.

## Home Assistant add-on (not in use)

An alternative to the section above, kept for reference: an add-on that replaces the standard one so that HA's Firmware update can build the speakers too.

HA's ESPHome firmware update entity builds and installs through the ESPHome Device Builder, and HA learns where that is only by Supervisor discovery from an add-on (its ESPHome config flow's `async_step_hassio`; there is no manual URL). The official add-on can't build baidu, so `addon/` is a local add-on that replaces it: the official image (`ghcr.io/esphome/esphome-hassio:2026.10.2`, Debian trixie, Device Builder 1.25.0) plus
- GCC 11, Ubuntu 22.04's armhf cross compiler, as in `smartspeaker-cross`. Debian trixie's GCC 14 doesn't do: its static libstdc++ needs GLIBC 2.34–2.38 symbols (the time64 functions, `getentropy`, `arc4random`, C23 `strtol`). The files of Ubuntu's packages are copied in; the image build checks that their runtime libraries resolve.
- `/sysroot` (Ubuntu xenial armhf, as `Dockerfile.dockcross`, plus stretch's OpenSSL 1.1.0) and `/opt/baidu-tp` (`third_party/build.sh`).

Everything else is the official add-on, and only configs with `baidu_host:` see the cross compiler, so ESP32 and other devices build as before. Its `config.yaml` is the official one with a different name, slug (`esphome_baidu`) and version, and no `image:` (HA builds it locally on the Pi; arm64 and amd64).

To install:
1. `addon/prepare.sh` (from `baidu/`) writes `.esphome/addon/addons/esphome_baidu` (the add-on) and `.esphome/addon/esphome` (the speakers' configs).
2. On the HA host (Samba share or the SSH add-on), copy `addons/esphome_baidu` to `/addons/esphome_baidu`, and the contents of `esphome/` into `/config/esphome` (a `baidu/` folder with the shared package and the components, and `baidu-<unit>.yaml` for each speaker). Add `ota_password` (the one in `baidu/secrets.yaml`) to `/config/esphome/secrets.yaml`.
3. In HA: Settings → Add-ons → Add-on Store → ⋮ → Check for updates; "ESPHome Device Builder (baidu)" appears under Local add-ons. Install it (it builds the image on the Pi).
4. Stop the official ESPHome add-on and turn off its start on boot, then start this one. Both use `/config/esphome`, so the existing devices stay listed.
5. HA then shows a Firmware update entity for each speaker whose config is in `/config/esphome`; Install compiles it in the add-on and uploads it over WiFi (the device must already run baidu with the same `ota_password`).

Tested here (2026-11-12), not yet on the HA host: the image builds for amd64 and, under emulation, for arm64. In the amd64 image baidu compiled (after a clean) against GLIBC ≤ 2.17 only, was uploaded to 143a00 over WiFi, and runs. In the arm64 image a threaded C++ test program cross-compiled with the same flags and `glibc_compat.c` needs only GLIBC 2.4/2.17 and runs on the speaker (a full baidu build under emulation would take very long). `baidu-143a00.yaml` validates in the `/config/esphome` layout.

## Components

### `baidu_mic` (microphone)

```yaml
microphone:
  - platform: baidu_mic
    id: mic
    device: hw:audiocodec       # default
    echo_cancellation: true     # default
```

Captures the codec at 16 kHz and outputs 16-bit mono. With `echo_cancellation` it reads 3 channels (MIC1, MIC2, and MIC3, which the board wires to the speaker signal) and runs Speex AEC against channel 3 and then Speex noise suppression; without it, 2 channels and neither. In both cases the two mics are averaged and DC-blocked, and the first 200 ms after opening are dropped (ADC power-up click). Frames are 512 samples (32 ms), as in the C++ app's `src/audio/alsa/input.cpp`, which this is ported from. Mic gain is not set here; the launcher sets `MIC1/MIC2 gain volume` to 3, as the C++ app's does.

Like ESPHome's I2S microphone, `start()`/`stop()` count listeners, `loop()` starts a capture thread when there is one and stops it when there are none, and the data callbacks run on the capture thread.

Measured on the unit with `tests/baidu_mic.yaml` (2026-11-12): 8.8% of one core and 2.9 MB RSS while capturing (the C++ app's capture + AEC was 9%). Pink noise at -12 dBFS played at LINEOUT 16: -29 dBFS from the raw mics; with echo cancellation -37 dBFS for the first 2 s, then -63 to -70 dBFS (room floor about -69 dBFS raw, -88 dBFS after noise suppression).

### `baidu_speaker` (speaker) and `baidu_media_player` (media_player)

```yaml
speaker:
  - platform: baidu_speaker
    id: spk
    device: hw:audiocodec            # default
    volume_control: LINEOUT volume   # default

media_player:
  - platform: baidu_media_player
    id: player
    name: Media Player
    speaker: spk
    volume_increment: 5%             # default
    volume_initial: 50%              # default; later the saved volume
```

`baidu_speaker` is the only writer to the playback PCM: 48 kHz mono S16 in 20 ms periods (the codec has one DAC). A mixer thread sums its inputs, which are the media player's media source (duckable), its announcement source, and the speaker's own `play()` stream (16-bit mono or stereo at any rate, downmixed and resampled). Volume is the codec's `LINEOUT volume` control scaled linearly over its range, so the echo-cancellation reference, tapped after it, follows the volume; mute zeroes the output. `baidu_speaker.apply_ducking` (`id`, `decibel_reduction`, `duration`) has the same shape as Voice PE's `mixer_speaker.apply_ducking` and ramps the media input only. Ported from the C++ app's `src/audio/mixer.cpp` and `alsa/volume.cpp`.

`baidu_media_player` has a media and an announcement pipeline. Each fetches one item at a time on its own thread: `http(s)://` through libcurl, or `file://` and plain paths. It decodes FLAC, MP3 or WAV with dr_libs (`dr_libs_VERSION`), then downmixes and resamples to 48 kHz mono. HTTP stall handling is as in the C++ app: media gives up after 30 s without data, and announcements restart after 5 s, up to twice, skipping the bytes already received. Commands follow ESPHome's speaker media player:
- a URL replaces the pipeline's playlist, or is appended with `ENQUEUE`, as `start_conversation` does after its pre-announcement
- `announcement: true` selects the announcement pipeline, also for `STOP`
- `PLAY`, `PAUSE`, `TOGGLE`, `MUTE`, `UNMUTE`, `VOLUME_UP`, `VOLUME_DOWN` and volume set

The state is ANNOUNCING while an announcement plays; the API reports that to HA as PLAYING, as ESPHome does for every media player. Volume and mute are saved in the preferences. Both purposes advertise FLAC 48 kHz mono 16-bit, so HA's ffmpeg proxy and TTS deliver the mixer's own format. Ported from the C++ app's `src/audio/{source,stream,decoder}.cpp`, with two fixes:
- the reader thread, not the main loop, flushes a source's ring buffer on stop, and a new item waits for that flush, so its start isn't dropped
- the stream keeps its first 64 KB for backward seeks: dr_mp3 reads 10 bytes to probe for an ID3 tag and then seeks back to 0, which failed for every MP3 read from the start

Tested on the unit (2026-11-12) with `tests/baidu_media_player.yaml` driven by aioesphomeapi through `adb forward tcp:6053 tcp:6053` (the unit's WiFi was failing, so media came from `file://`; HTTP was only exercised by a refused connection):
- supported formats as above
- volume set and step map to LINEOUT 9 and 11, and the volume is restored after a restart
- a 44.1 kHz stereo MP3 plays at 8.8% of one core (4.1 MB RSS); pause, play and stop work
- announcements play over media; an announcement with an enqueued second item plays both, then ends
- `STOP` with `announcement` ends an announcement mid-way; a refused URL ends its announcement at once
- recorded on the codec's reference channel (MIC3), the music sat at about -33 dBFS, dropped about 20 dB for a 3 s announcement, and ramped back over about 1 s

### `baidu_leds` (light)

```yaml
light:
  - platform: baidu_leds
    id: led_ring
    name: LED Ring
    path: /sys/class/leds/isled01   # default
    num_leds: 12                    # default
    effects:
      - addressable_rainbow:
```

The 12-LED ring as an addressable light, so ESPHome's light effects and per-LED lambdas work, and Voice PE-style LED phases can be written in YAML. At setup it writes `1` to `user_space` (taking the ring from the kernel's effects) and `255` to `brightness`; each frame goes to `set_all_led_color` as twelve decimal 0xRRGGBB values, and unchanged frames are skipped. On shutdown it writes `user_space` 0, handing the ring back. Ported from the C++ app's `src/leds.cpp`.

Tested on 143a00 (2026-11-12) over the API: solid red, green and blue, the rainbow and twinkle effects, and off. With the rainbow running at ESPHome's default rate the process used about 2% more of a core than with the ring idle, and the writes added nothing to the kernel log.

### `evdev` (binary_sensor)

```yaml
binary_sensor:
  - platform: evdev
    name: Play/Pause Key
    key: KEY_PLAYPAUSE          # or KEY_MUTE, KEY_VOLUMEDOWN, KEY_VOLUMEUP, or a code
    device: /dev/input/event0   # default (sunxi-keyboard)
```

A key of a Linux input device as a binary sensor that is on while the key is held, so `on_press`, `on_click` and `on_multi_click` work as with a GPIO button. Autorepeat events are ignored. Each sensor opens the device itself (evdev gives every reader every event), reads it non-blocking in `loop()`, and takes its initial state from `EVIOCGKEY`. Every down and up is logged at DEBUG, as ESPHome 2026.10 doesn't log binary sensor changes. Ported from the C++ app's `src/evinput.cpp`.

Tested on 143a00 (2026-11-12): pressing Mute once, Volume Down twice, Volume Up three times and Play/Pause four times gave exactly those counts on `KEY_MUTE`, `KEY_VOLUMEDOWN`, `KEY_VOLUMEUP` and `KEY_PLAYPAUSE`, each with a matching up, and an `on_multi_click` single click on Play/Pause fired. The double-click and long-press timings were not exercised on Play/Pause.

### `micro_wake_word` (host port)

`components/micro_wake_word` overrides ESPHome 2026.10.2's component of the same name, so Voice PE's `micro_wake_word:` YAML (models from the manifests, `on_wake_word_detected`, `voice_assistant: micro_wake_word:`, the `start`/`stop`/`enable_model` actions) works unchanged. Changes from upstream:
- `__init__.py`: allowed on `host`. On host, instead of the ESP-IDF components, it adds the include paths and `-ltflm -lmicro_speech_features` from `/opt/baidu-tp` in the image.
- The C++ files are upstream's with `USE_ESP32` guards widened to `USE_HOST`. `freertos_host.h` supplies what the inference task uses (event group, queue, `StaticTask`, `vTaskSuspend`/`vTaskResume`) on std::thread, so the task logic is unchanged. Suspending the task for an OTA is a no-op on host.
- `neon_kernels.{h,cpp}` (new): NEON int8 `CONV_2D` and `DEPTHWISE_CONV_2D`, which `streaming_model.cpp` registers on host in place of TFLM's. They keep TFLM's Init and Prepare and replace Eval with the reference arithmetic, so the output is bit-identical; other cases (non-int8, grouped conv, depth multiplier ≠ 1) go to TFLM's Eval. The reference kernels recompute a 4-D index per multiply-add (about 75 cycles each on the Cortex-A7) and took 90% of an inference.
- The audio threads are named (`mww`, `baidu_mic`, `baidu_speaker`) for `top -H`.

The libraries are built into the `baidu-esphome` image by `third_party/build.sh` (run from the Dockerfile): TFLite Micro from `third_party/tflite-micro`, the esp-tflite-micro version ESPHome pins (copied from the C++ app's `third_party`, reference kernels instead of ESP-NN), and the spectrogram frontend from `third_party/micro_speech_features`, v1.2.3, the version ESPHome now pins (the C++ app has v1.1.0). Re-check both when changing the pinned ESPHome version.

Tested on 143a00 (2026-11-12, 802.11b-only WiFi) with `baidu-xdh-1d-a1.yaml` (Okay Nabu, Hey Jarvis, Hey Mycroft; the first enabled): "Okay Nabu" detected at a sliding-average probability of 0.97 to 0.99, `voice_assistant` ran the session (including HA's follow-up question, "What day is it?" → "It's Thursday, November 12 2026."), and detection restarted when it ended. While detecting, the whole process used 17% of one core and 6.8 MB RSS (4 threads); the C++ app used 16% in total. The reference kernels are the likely cost; ESP-NN is ESP32-only.

With the NEON kernels (2026-11-13): a test program running each model with both kernel sets on the same random input for 3,000 inferences found no output difference, and one inference took 161 µs instead of 1,659 (Okay Nabu), 138 instead of 1,477 (Hey Jarvis), 143 instead of 1,640 (Hey Mycroft) and 112 instead of 1,240 (Stop). A model runs every 30 ms. In the app the `mww` thread went from 7.0% to 1.9% of a core and the process from 17.4% to 12.3%; the rest is mostly `baidu_mic` (Speex echo cancellation and noise suppression, 9.5%). Before, the wake word logged `Not enough free bytes in ring buffer` about ten times in a day, several during CPU-heavy WiFi tests.

### `baidu_mdns`

```yaml
mdns:
baidu_mdns:
```

ESPHome's `mdns` builds its service records on host but sends nothing (`mdns_host.cpp`). `baidu_mdns` answers queries for those records (PTR, SRV and TXT for each service, A for `<name>.local`; IPv4, UDP 5353) and announces them three times at startup, so HA discovers the device as it does an ESP32. It calls `mdns.enable_mdns_storage()`, ESPHome's hook for external components, to read the records through `MDNSComponent::get_services()`. ESPHome fixes the encryption TXT at boot; `baidu_mdns` instead follows the live key state (`api_encryption` once HA has provisioned a key, else `api_encryption_supported` plus `api_provisioning=zero-psk`) and announces again when it changes. Ported from the C++ app's `src/esphome/mdns.cpp`.

Checked on 143a00 (2026-11-12): `avahi-browse -rt _esphomelib._tcp` on the LAN lists `baidu-143a00` with ESPHome's TXT records, and `baidu-143a00.local` resolves.

## Spike results (2026-11-12)

- The build works: the program (2.0 MB, 1.1 MB stripped) needs GLIBC ≤ 2.17 and only `libcrypto.so.1.1`, `libm`, `libpthread` and `libc`, all on the device.
  - ESPHome's host `md5`/`sha256` use OpenSSL's 1.1 EVP API. The device has OpenSSL 1.1.0i with `OPENSSL_1_1_0` symbol versions, which stretch's 1.1.0l matches.
- On the unit (run from `/tmp/baidu` with `ESPHOME_PREFDIR`, the C++ app stopped), it starts cleanly: API on 6053 (encryption supported, provisionable), OTA on 8082. VSZ is about 5 MB.
- **mDNS is a stub on host.** `mdns_host.cpp` builds the records but sends nothing, so HA can't discover the device. `baidu_mdns` (above) fills the gap.
- **OTA works.** Over WiFi both attempts stalled (at 97% and 15%) because the unit's WiFi was failing at the time: deauths from both "Home" APs every ~30 s, 50–95% loss on traffic *to* the unit, while its own outgoing traffic got through. Through USB instead (`adb forward tcp:8082 tcp:8082`, then `scripts/esphome.sh upload … --device 127.0.0.1`), the update took 0.95 s: 2.0 MB sent deflated as 0.78 MB, staged as `<binary>.ota.new`, renamed over the running binary, then `execv` (same PID) into the new build. The `adb forward` route is also a fallback for updating a unit with bad WiFi.
- **OTA over WiFi works** with the unit in 802.11b-only mode (the C++ app's `[wifi] legacy_rates`, applied by its launcher): `scripts/esphome.sh upload … --device 192.168.166.210` sent 2.36 MB in 2.6 s on 2026-11-12, and the process restarted in place (same PID).
- The OTA handler blocks ESPHome's main loop while it receives (`took a long time for an operation (277322 ms)`), so audio must run on its own threads, as planned.
- **`voice_assistant` builds on host with the overrides above.** As shipped it doesn't: `voice_assistant` auto-loads `audio` (ESP32-only, ESP-IDF libraries) and `ring_buffer` (FreeRTOS `xRingbuffer`), and `audio_transfer_buffer.h` is wrapped in `USE_ESP32`. The `voice_assistant` code itself is portable (API, sockets, no FreeRTOS). A test config with `voice_assistant` and a do-nothing microphone compiled and linked (2.3 MB, GLIBC ≤ 2.17). It has not been run against HA yet.
- The spike YAML uses MAC `4A:F3:F3:14:86:A2` (the real one with the locally administered bit), so HA treats it as a new device and leaves the C++ app's `xiaodu-1486a2` entry alone.

## Verified against HA (2026-11-12, unit 143a00 on the Home-Router-2X SSID)

- HA discovers `baidu-143a00` through `baidu_mdns`, and **Configure** provisions the key without asking (`Noise PSK saved`); the TXT then switches to `api_encryption`.
- `assist_satellite.announce`: HA sends the pre-announce chime (ffmpeg proxy) and the TTS (`tts_proxy`), both as FLAC 48 kHz mono; both play in full on the announcement source, and `voice_assistant` reports the announcement finished. Volume changes from HA reach the codec.
- `assist_satellite.start_conversation`: chime and prompt, then `voice_assistant` starts `baidu_mic` and streams to HA as the prompt ends. HA's VAD found the speech, STT gave "What time is it?", and the reply ("5:39 PM") played about 2 s after the speech ended; then back to IDLE.

- Music Assistant: the player streams MA's flow URL over HTTP, FLAC 48 kHz stereo (MA's per-player codec choice; downmixed, no resampling), with no stalls. The whole process used 2.1% of one core and 6.1 MB RSS while playing.

## Still to verify in the spike

- Ducking of MA music under a voice session.
