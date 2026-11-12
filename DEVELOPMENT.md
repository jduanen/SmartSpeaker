# Development of the Home Assistant Satellite Assistant Application on the Baidu SmartSpeaker

This documents the process and status of the development of the HA Satellite Assistant as defined in [README.md](README.md).

## Development Phases

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
- Regenerate the protobuf code after updating `proto/`: `pip install nanopb==0.4.9.1 grpcio-tools`, then `scripts/gen_proto.py`. String, bytes and repeated fields are heap pointers (`PB_ENABLE_MALLOC`); call `pb_release()` after decoding a message that has them.
- On a host without the Xiaodu codec, set `[audio] input=null` and `output=null` (ALSA's null device) and disable `[leds]`/`[buttons]`.
- Wake word test on WAVs (16 kHz mono): `ninja -C builddir test/wakeword_wavs`, then run it under QEMU from a scratch directory (it writes `config.ini`):
  `/sysroot/lib/arm-linux-gnueabihf/ld-2.23.so --library-path /sysroot/lib/arm-linux-gnueabihf:/sysroot/usr/lib/arm-linux-gnueabihf builddir/test/wakeword_wavs share/models okay_nabu [--stop] [--sensitivity 0|1|2] *.wav` (in the cross image, with the host's binfmt qemu-arm).

**3. Audio engine** (new `src/audio/{mixer,source,decoder,aec}.cpp`; the existing ALSA input is reused)
- **Playback:**
  - one ALSA PCM at 48 kHz mono s16 with a period of about 20 ms (both line outputs carry DACL, so the speaker is mono; downmix stereo sources)
  - mixer thread sums the media and announce sources, applies the duck gain with a ramp, then master volume
- **Sources:** each runs a libcurl write callback into a decoder (dr_flac/dr_mp3/wav), resamples if needed, and fills a ring buffer. Each has states idle, playing, paused and finished.
- **Capture:** ALSA at 48k, 3 channels (MIC1, MIC2, speaker reference). DC-block the mics, drop the first 200 ms (ADC power-up click), downsample to 16k, average the mics to mono, then speex echo cancellation against channel 3, then speex noise suppression, then fan out to the wake word and the session.
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

## Status Checklist (updated as work proceeds)

- [ ] Phase 0 Probe stock unit, backup, install/uninstall scripts, config fix
  - [x] scripts: `probe.sh`, `backup.sh`, `install.sh`, `uninstall.sh`, `dcslaunch.sh`
  - [x] `Config::load()` reads `[audio]` and `[esphome]`
  - [x] README: stock-unit install/uninstall and WiFi
  - [x] probe a Genie unit → `notes/stock.txt` (no OTA updater found, `OTA_DISABLE` stays empty; AEC reference found)
  - [ ] probe a DuerOS unit (no Genie) for a DuerOS-side updater
  - [x] backup of the Genie unit (`backups/<serial>/`, app.img fsck-clean after journal replay)
  - [x] verify on device: Phase 1 binary run from `/tmp/ss` with Genie stopped; `ldd` resolves only to `/lib`, `/usr/lib`; LEDs, keys, ALSA tone, 16 kHz capture OK
  - [x] free `/opt` (removed `/opt/genie`; 43 MB free) and `install.sh` on the Genie unit
- [x] Phase 1 Strip and re-base build
  - deps are glib-2.0, alsa, speexdsp, threads; gio and libcurl get added in the phases that use them
  - PLAYPAUSE is push-to-talk (Listening ↔ Sleeping); no wake word until Phase 5
  - `glibc_compat.c` (ARM syscall numbers) is built only for the armhf target
  - removed the vendored glib 2.74 headers in `include/glib*`; the build uses the sysroot's glib 2.48
- [x] Phase 2 ESPHome API core
  - [x] `proto/` (aioesphomeapi v46.29.0), `third_party/nanopb` (0.4.9.1), `scripts/gen_proto.py` → `src/esphome/api.pb.c`, `include/esphome/{api.pb.h,api_ids.h}`
  - [x] plaintext server on :6053 (API 1.10, as linux-voice-assistant): Hello, DeviceInfo, DeviceCapabilities, ListEntities, SubscribeStates, Ping, Disconnect
  - [x] entities: media_player (volume live), Mute microphone (drives Disabled), Wake sound (gates the tone), Wake word sensitivity, LED ring, Button press (the wake word select was replaced in Phase 5)
  - [x] verified with aioesphomeapi 46.28.0 against the x86 build (list, states, commands, keepalive)
  - [x] verified in HA 2026.11.1 (added by IP): entities appear, mute switch toggles the muted LEDs, connection stays up
- [x] Phase 3 Audio engine (ALSA mixer, decoders, AEC)
  - [x] `Mixer`: one ALSA writer, 48 kHz mono S16, 20 ms periods; media + announce sources; duck gain with ramp (Listening ducks -20 dB at once, Sleeping restores over 1 s)
  - [x] `Source` → `ByteStream` (libcurl multi or file) → `Decoder` (dr_flac/dr_mp3/dr_wav @ a466dbe) → downmix → speex resampler → ring buffer
  - [x] capture: 3 ch at 16 kHz, mics averaged + DC-blocked, first 200 ms dropped, speex AEC against ch3 + preprocess (`[ec] enabled/loopback`, `[audio] stereo2mono`)
  - [x] HA media_player: play_media/play/pause/stop/toggle drive the media source; state IDLE/PLAYING/PAUSED/ANNOUNCING
  - [x] CPU on the unit (`-O2`): 9% of one core idle (capture+AEC), 17–18% playing 44.1 kHz MP3/FLAC; RSS ~7 MB
  - [x] echo test (`-Ddump_streams=true`, music at volume 19%): AEC converges in ~10 s, then removes 15–33 dB (typ. ~25 dB); no clipping at that volume
  - [x] FLAC from the HA media folder plays over HTTP; HA play/pause/stop/volume work
  - [x] the MP3 failures were the unit not receiving broadcasts (ARP from HA failed; 7–15 s TCP connects); reassociating fixed it, and `scripts/wifi_watchdog.sh` now does that automatically
  - [x] the ~10 s start delay for files under `/media/local/Music` went away once HA's ffmpeg proxy delivers FLAC 48 kHz mono (Phase 6)
  - volume stays on the codec's LINEOUT (not a mixer master gain): the AEC reference is tapped after the amp, so it follows volume
  - mic gain (MIC1/2 = 6, i.e. 39 dB, from the Genie launcher) clipped at high volume; lowered to 3 in Phase 6
- [x] Phase 4 Voice pipeline
  - [x] `src/voice/session.cpp` (`VoiceSession`): VoiceAssistantRequest/Response, mic audio buffered until HA accepts, then streamed as VoiceAssistantAudio; HA pipeline events → state events; ERROR → stop; feature flags VOICE_ASSISTANT|API_AUDIO|TIMERS|ANNOUNCE|START_CONVERSATION
  - [x] states: Sleeping → (PLAYPAUSE) Listening → (STT_VAD_END) Processing → (TTS URL) Saying → Sleeping, or Listening again if `continue_conversation`; Ringing for finished timers; Genie conversation/audio-control events and STT→Genie timing removed
  - [x] streaming TTS: the RUN_START URL plays at INTENT_PROGRESS `tts_start_streaming=1`, otherwise the TTS_END URL
  - [x] the wake sound is kept out of the mic stream (before the AEC adapts, STT heard it as "Beep, beep.")
  - [x] verified: push-to-talk "What time is it?" → "8:20 PM"; "Set a timer for one minute"; LLM follow-up question reopened the mic with the same conversation id
  - [x] verified: timer ring (stops on PLAYPAUSE), `assist_satellite.announce` with pre-announce chime, `assist_satellite.start_conversation` (speaks, then listens without a button press)
  - [x] TTS/announcement fetches restart after 5 s without data (up to 2 retries); music keeps a 30 s stall limit
  - processing LEDs are purple (`processing_color=8000ff`)
  - mute was the Disabled state here; Phase 7 made it a flag
- [x] Phase 5 microWakeWord
  - [x] `third_party/tflite-micro` (esp-tflite-micro v1.3.3.1, ESPHome's pin; reference kernels, no ESP-NN) and `third_party/micro_speech_features` (ESPMicroSpeechFeatures v1.1.0), built as static libraries by `third_party/meson.build`
  - [x] models in `share/models` (pushed by `install.sh` to `[esphome] model_dir`): okay_nabu, hey_jarvis, hey_mycroft, stop; each is `<id>.json` (v2 manifest) + `<id>.tflite`; provenance in `share/models/VERSION`
  - [x] `src/audio/wakeword.cpp` (`WakeWord`): port of ESPHome `micro_wake_word` (40 features / 30 ms window / 10 ms step, streaming models, mean of the last `sliding_window_size` probabilities vs the cutoff, 1 s cool-off after load or detection)
  - [x] HA picks the wake word through VoiceAssistantConfigurationRequest/SetConfiguration (one active, as ESPHome); the old "Wake word" select is gone (delete the stale entity in HA); "Wake word sensitivity" uses Voice PE's cutoffs; both saved to `config.ini` `[wakeword]`
  - [x] states: wake word in Sleeping → Listening (sends `wake_word_phrase`; only audio after the word goes to HA); in Saying it interrupts and listens again; in Ringing it, or "stop", silences the timer; "stop" in Saying ends the reply; the stop model runs only in Saying and Ringing; muted (Disabled) ignores it
  - [x] host test `test/wakeword_wavs` on Piper TTS clips (3 voices × 2 speeds): okay_nabu 6/6, hey_jarvis 6/6, hey_mycroft 4/6, stop 6/6; no cross-triggers between wake words; near-misses "Okay, now you" (okay_nabu 2/6) and "Start the stopwatch" (stop 3/6)
  - [x] on the unit: 16% of one core idle (was 9% before the wake word), RSS 7.3 MB
  - [x] real voices on the unit: Okay Nabu (0.86–0.94) close, across the room and over music; Hey Mycroft after switching in HA (1.00), choice saved to `[wakeword]`; "stop" cut off a streamed LLM reply 11 s in (0.68)
  - [x] "stop" (0.69) and Okay Nabu (0.92) each silenced a ringing timer 2 s in
- [x] Phase 6 Media player, ducking, volume
  - [x] `supported_formats`: FLAC, 48 kHz, mono, 16-bit for both media and announcements, so HA's ffmpeg proxy and TTS deliver the mixer's own rate (no resampling, no MP3); Music Assistant reads the same list from HA diagnostics
  - [x] media_player mute silences all output (`Mixer::set_muted`), as ESPHome's speaker does
  - [x] media_player announcements (Music Assistant, `play_media` with announce) go through Saying (duck, LEDs, "stop" word) without VoiceAssistantAnnounceFinished
  - [x] verified with Music Assistant: playback, ducking under a voice question and reply, recovery, mute, announcements, voice replies as FLAC
  - [x] mono is the hardware: the codec (sun8iw18) has one DAC (DACL); both LINEOUT muxes can only select DACL or nothing, and both drive one amplifier as a pair (muting the right side measured -6 dB with a mic, nothing goes silent). Genie opened the PCM as 2 channels into the same DAC
  - [x] mic headroom: pink noise at -1 dBFS, mics recorded at LINEOUT 16/22/27/31 (HA volume maps linearly to 0–31). At Genie's MIC1/2 gain 6 (39 dB) the mics compress from LINEOUT ~22 (~70%) and clip above ~27; at gain 3 (30 dB) they stay linear through 27 and only touch full scale at 31. `dcslaunch.sh` now sets 3. The reference (MIC3, 0 dB) touches full scale at 31
  - [x] wake word and STT across the room at mic gain 3, also with music fairly loud (0.86–0.93)
  - Music Assistant still streams MP3 44.1 kHz stereo (its per-player output codec); set it to FLAC in MA to skip MP3 decoding and resampling
- [x] Phase 7 LEDs, buttons, mute, sounds
  - [x] `src/leds.cpp` rewritten: frames rendered from app state in Satellite1's `control_leds` priority, on 12 LEDs: init (blue twinkle) / no HA (red twinkle) → volume bar (1 s after VOL±) → timer ring (whole ring pulsing) → listening (clockwise spin, two heads) → thinking (two opposite LEDs breathing) → replying (counter-clockwise spin) → error (red pulse, 2 s) → idle (the "LED ring" light, or off) with four red LEDs when the mic is muted. Phases use the light's color (default HA blue 17bbf2) at ≥20% brightness. A 50 ms timer runs only while animating; unchanged frames aren't written (the is31fl3236 driver logs every write)
  - [x] the "LED ring" light is real (idle color, accent color) and saved to `[leds] ring/ring_color/ring_brightness`; the per-state `*_effect`/`*_color` keys are gone
  - [x] mic mute is a flag (`App::set_mic_muted`), not a state (Disabled removed): no wake word, no frames, a running Assist stops, no follow-up listening, PLAYPAUSE won't start Assist; timers, replies and announcements work as usual; MUTE key and the HA switch both toggle it, with the Voice PE mute sounds
  - [x] PLAYPAUSE (Voice PE timings: presses < 1 s, gaps < 0.25 s, hold 1 s): single = stop the ring, else the pipeline, else the reply/announcement, else pause music, else start Assist; double/triple/long fire the HA "Button press" event with their Voice PE sounds; VOL± are ±5% on key down (and autorepeat)
  - [x] sounds: HA Voice PE FLACs in `share/sounds` (CC BY 4.0, Clayton Charles Tapp; `share/sounds/LICENSE.md`, `VERSION`); feedback sounds are skipped while a reply, announcement or ring plays
  - [x] idle on the unit: 16% of a core, RSS 7.1 MB (unchanged)
  - [x] verified on the unit: startup twinkle after a power cycle, LEDs in each phase, mute key/switch, PLAYPAUSE single/double/triple/long (HA events), VOL± bar, ring light from HA
- [ ] Phase 8 Encryption, mDNS, launcher
  - [x] Noise_NNpsk0_25519_ChaChaPoly_SHA256 (`src/esphome/noise.cpp`; X25519 and ChaCha20-Poly1305 from `third_party/monocypher` 4.0.3, SHA-256/HMAC/HKDF in noise.cpp), following ESPHome's api_frame_helper_noise: prologue `NoiseAPIInit` + client hello, server hello `0x01 name\0 mac\0`, handshake reply `0x00`+msg or `0x01`+reason, then frames of encrypted `type16 len16 protobuf`
  - [x] key handling as current ESPHome: with `[esphome] encryption_key` only Noise with that key; without one, plaintext or Noise with the all-zeros key, and DeviceInfo says `api_encryption_supported` + `api_encryption_provisionable`, so HA generates a key and pushes it over a zero-key Noise session (NoiseEncryptionSetKeyRequest; empty clears). The key is written to `config.ini`, then activated by disconnecting every client
  - [x] verified with aioesphomeapi against the unit: plaintext and zero-key sessions before a key; provisioning; afterwards plaintext → "requires encryption", zero/wrong key → "invalid encryption key", right key → full session; clearing the key restores plaintext
  - [x] verified with HA 2026.11.1: HA provisioned a key on its own and reconnects encrypted after reboots (by accident: a test instance on port 16053 with the same MAC was discovered as this device; the key was then moved into `/opt/ss/etc/config.ini`)
  - [x] mDNS responder (`src/esphome/mdns.cpp`): `<name>._esphomelib._tcp.local` PTR/SRV/TXT and `<name>.local` A, ESPHome's TXT keys (friendly_name, version, mac, platform=linux, board, network, api_encryption or api_encryption_supported, project_name/version); announces at start and on key changes, retries the multicast join until WiFi is up; HA followed the port change through it
  - [x] `dcslaunch.sh` waits up to 60 s for wlan0 to get an address before starting NTP and smartspeaker
  - [ ] a stock DuerOS unit: `install.sh`, reboot, discovered by HA and provisioned with encryption (no stock unit yet)
  - the key lives only in the unit's `/opt/ss/etc/config.ini` (and HA); `backups/` predates it. Removing it there (`encryption_key=`) and restarting returns the unit to provisionable plaintext, and HA re-provisions on its next connect

