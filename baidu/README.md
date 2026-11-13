# baidu: Using the Xiaodu Smart Speaker as an ESPHome Device

This is an ESPHome (host platform) firmware for the Baidu Xiaodu XDH-1D-A1, configured by YAML and updated by ESPHome OTA. It is separate from the C++ app in the parent directory, which stays as it is; that one names itself `xiaodu-…`, this one `baidu-…`.
## Build

```bash
docker build --network host -f baidu/Dockerfile -t baidu-esphome baidu   # once
cd baidu
echo "ota_password: \"$(openssl rand -hex 16)\"" > secrets.yaml         # once; gitignored
scripts/esphome.sh compile baidu-xdh-1d-a1.yaml
scripts/esphome.sh upload baidu-xdh-1d-a1.yaml --device <ip>             # OTA
```

- `baidu/Dockerfile` is `smartspeaker-cross` plus ESPHome 2026.10.2 (Python 3.12 via uv) and Debian stretch's OpenSSL 1.1.0 armhf headers and library in `/sysroot`.
- `scripts/esphome.sh` runs `esphome` in that image with `CC`/`CXX` set to the armhf cross compiler and the sysroot flags from `../scripts/cross_file.txt`, plus `-pthread` (glibc 2.23 keeps pthreads in libpthread) and `-O2` (the host build adds no optimisation flag; at `-O0` the microphone alone used 24% of a core).
- The program is `.esphome/build/<name>/.pioenvs/<name>/program`.
- `components/baidu_host` carries `glibc_compat.c` (copied from `../src`, plus `pthread_cond_clockwait`, which GCC 11's libstdc++ uses for `condition_variable::wait_for` and glibc 2.23 lacks) and `host_shim.h` (FreeRTOS tick types and `esp_err_t` for host).
- `components/audio` and `components/ring_buffer` override ESPHome 2026.10.2's components of the same name so `voice_assistant` builds on host. Changes from upstream:
  - `audio/__init__.py`: allowed on `host` as well as `esp32`; `to_code` skips the ESP-IDF libraries on host.
  - `audio/audio_transfer_buffer.{h,cpp}`: compiled under `USE_HOST` too, with `host_shim.h` in place of the FreeRTOS/ESP-IDF headers, and `Speaker::play()` without the ESP32-only ticks argument. The decoder, reader and resampler stay ESP32-only.
  - `ring_buffer/__init__.py`: no `esp32` dependency. `ring_buffer.h` gains a `USE_HOST` class with the same interface, implemented in `ring_buffer_host.cpp` (byte ring, std::mutex/condition_variable, ticks are ms).
  Re-check these against upstream when changing the pinned ESPHome version.

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

### `micro_wake_word` (host port)

`components/micro_wake_word` overrides ESPHome 2026.10.2's component of the same name, so Voice PE's `micro_wake_word:` YAML (models from the manifests, `on_wake_word_detected`, `voice_assistant: micro_wake_word:`, the `start`/`stop`/`enable_model` actions) works unchanged. Changes from upstream:
- `__init__.py`: allowed on `host`. On host, instead of the ESP-IDF components, it adds the include paths and `-ltflm -lmicro_speech_features` from `/opt/baidu-tp` in the image.
- The C++ files are upstream's with `USE_ESP32` guards widened to `USE_HOST`. `freertos_host.h` supplies what the inference task uses (event group, queue, `StaticTask`, `vTaskSuspend`/`vTaskResume`) on std::thread, so the task logic is unchanged. Suspending the task for an OTA is a no-op on host.

The libraries are built into the `baidu-esphome` image by `third_party/build.sh` (run from the Dockerfile): TFLite Micro from `third_party/tflite-micro`, the esp-tflite-micro version ESPHome pins (copied from the C++ app's `third_party`, reference kernels instead of ESP-NN), and the spectrogram frontend from `third_party/micro_speech_features`, v1.2.3, the version ESPHome now pins (the C++ app has v1.1.0). Re-check both when changing the pinned ESPHome version.

Tested on 143a00 (2026-11-12, 802.11b-only WiFi) with `baidu-xdh-1d-a1.yaml` (Okay Nabu, Hey Jarvis, Hey Mycroft; the first enabled): "Okay Nabu" detected at a sliding-average probability of 0.97 to 0.99, `voice_assistant` ran the session (including HA's follow-up question, "What day is it?" → "It's Thursday, November 12 2026."), and detection restarted when it ended. While detecting, the whole process used 17% of one core and 6.8 MB RSS (4 threads); the C++ app used 16% in total. The reference kernels are the likely cost; ESP-NN is ESP32-only.

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
