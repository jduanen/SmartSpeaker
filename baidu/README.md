# baidu: the Xiaodu speaker as an ESPHome device

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
- `scripts/esphome.sh` runs `esphome` in that image with `CC`/`CXX` set to the armhf cross compiler and the sysroot flags from `../scripts/cross_file.txt`, plus `-pthread` (glibc 2.23 keeps pthreads in libpthread).
- The program is `.esphome/build/<name>/.pioenvs/<name>/program`.
- `components/baidu_host` carries `glibc_compat.c` (copied from `../src`).

## Spike results (2026-11-12)

- The build works: the program (2.0 MB, 1.1 MB stripped) needs GLIBC ≤ 2.17 and only `libcrypto.so.1.1`, `libm`, `libpthread` and `libc`, all on the device.
  - ESPHome's host `md5`/`sha256` use OpenSSL's 1.1 EVP API. The device has OpenSSL 1.1.0i with `OPENSSL_1_1_0` symbol versions, which stretch's 1.1.0l matches.
- On the unit (run from `/tmp/baidu` with `ESPHOME_PREFDIR`, the C++ app stopped), it starts cleanly: API on 6053 (encryption supported, provisionable), OTA on 8082. VSZ is about 5 MB.
- **mDNS is a stub on host.** `mdns_host.cpp` builds the records but sends nothing, so HA can't discover the device. A `baidu_mdns` component that answers with those records is needed; the C++ app's `src/esphome/mdns.cpp` can be the base.
- **OTA:** the upload connects, authenticates, compresses (2.0 MB → 0.78 MB) and stages to `<binary>.ota.new`, but both attempts stalled, at 97% and 15%. At the same time a plain HTTP download of 2 MB from the host to the unit also failed after ~32 s, and 1 KB took 4 s, with deauths from both "Home" APs in `dmesg`. So the failures were the unit's WiFi, not OTA. The swap-and-`execv` step is still unverified.
- The OTA handler blocks ESPHome's main loop while it receives (`took a long time for an operation (277322 ms)`), so audio must run on its own threads, as planned.
- The spike YAML uses MAC `4A:F3:F3:14:86:A2` (the real one with the locally administered bit), so HA treats it as a new device and leaves the C++ app's `xiaodu-1486a2` entry alone.

## Still to verify in the spike

- OTA completes and the program restarts itself, once the WiFi is healthy.
- HA adopts it (by IP, until `baidu_mdns` exists) and provisions encryption.
- Whether `voice_assistant` and `ring_buffer` build on host.
