# SmartSpeaker Development Guide

How to build and run custom applications on the Baidu Xiaodu smart speaker (XDH-1D-A1).

---

## Hardware Summary

| Item | Detail |
|------|--------|
| SoC | Allwinner R328-S3 (sun8iw18p1) |
| CPU | Dual-core ARMv7 Cortex-A7 |
| RAM | 128 MB DRAM |
| Storage | 256 MB NAND flash |
| Kernel | Linux 4.9.118 |
| OS | TinaLinux 3.0.8 (OpenWRT 14.07 fork) |
| libc | GLIBC 2.23 (`/lib/libc-2.23.so`) |
| Shell | BusyBox 1.27.2 (ash) |

### Peripheral chips

| Chip | Role |
|------|------|
| AD52058 (ESMT) | Class D audio amplifier, 2×15 W |
| RTL8723DS (Realtek) | 802.11b/g/n WiFi + Bluetooth 4.2 |
| HTR3236 | 36-channel LED PWM driver (I²C) |

### Partition layout (NAND)

| Partition | Mount | FS | Mode | Size |
|-----------|-------|----|------|------|
| nand0p1 (env) | — | raw | rw | 128 KB |
| nand0p2 (private) | /private | vfat | ro | 123 KB |
| nand0p3 (pstore) | /sys/fs/pstore | pstore | rw | — |
| nand0p4 (bootA) | — | raw | — | — |
| nand0p5 (rootfsA) | / | squashfs | **ro** | 16 MB |
| nand0p6 (bootB) | — | raw | — | — |
| nand0p7 (rootfsB) | — | squashfs | — | 16 MB |
| nand0p8 (app) | /opt | ext4 | **rw** | ~46 MB |
| nand0p9 (UDISK) | /data | ext4 | **rw** | ~9 MB |

Custom applications live in `/opt/ss/` (created manually). The root FS is read-only.

---

## Device Access

### Connect via ADB (USB)

1. Connect the device to your host with a microUSB cable.
2. Power the device from its 12 V supply.
3. On the host:

```bash
adb devices          # confirm device appears
adb shell            # open a shell on the device
```

### Set up WiFi (one-time)

```bash
# On device via adb shell:
cp /data/wifi/wpa_supplicant.conf /data/wifi/wpa_supplicant.conf.orig
cat > /data/wifi/wpa_supplicant.conf << 'EOF'
network={
    scan_ssid=1
    key_mgmt=WPA-PSK
    ssid="your-ssid"
    psk="your-password"
}
EOF
echo "nameserver 192.168.1.1" > /data/wifi/resolve.conf
reboot
```

After reboot the device gets an IP; use `ip addr` or check your router.

### Enable SSH

```bash
# On device via adb shell:
/etc/init.d/sshd start
# or
dropbear -p 22
```

After that: `ssh root@<device-ip>`

### Create the app directories (one-time)

```bash
adb shell "mkdir -p /opt/ss/bin /opt/ss/lib"
```

---

## Track A: Deploy Apps to Existing TinaLinux

This is the fastest path to running custom code. You deploy binaries into `/opt/ss/` without
touching the root FS.

### The GLIBC version problem

The device has **GLIBC 2.23**. Ubuntu 18.04's `arm-linux-gnueabihf-gcc 7.5` links against the
host sysroot, which can emit references to GLIBC symbols from 2.28 or later, causing:

```
/lib/libc.so.6: version 'GLIBC_2.28' not found
```

Simple C programs that use only basic syscalls work fine as static binaries (proven by
`test/helloworld.c` and `test/glibcTest.c`). Complex C++ apps using glib need one of the
workarounds below.

### Option 1: Fix Dockerfile.dockcross (recommended for dynamic linking)

The `Dockerfile.dockcross` already uses a more compatible base. It needs two additions:

1. Enable dpkg multi-arch so ARM dev packages are available:

```dockerfile
RUN dpkg --add-architecture armhf && apt-get update && \
    apt-get install -y libglib2.0-dev:armhf libasound2-dev:armhf
```

2. Add `pkg_config_libdir` to `scripts/cross_file.txt`:

```ini
[properties]
pkg_config_libdir = '/usr/lib/arm-linux-gnueabihf/pkgconfig:/usr/share/pkgconfig'
```

Without this, meson's pkg-config looks at the host's (x86) library paths and fails:
`Dependency "glib-2.0" not found, tried pkgconfig`

After the fix, build with:

```bash
docker build -f Dockerfile.dockcross -t smartspeaker-cross .
docker run --rm -v $(pwd):/app smartspeaker-cross bash -c \
  "cd /app && meson setup --wipe --cross-file=scripts/cross_file.txt builddir && ninja -C builddir"
```

Verify GLIBC requirements of the output:

```bash
arm-linux-gnueabihf-objdump -p builddir/src/smartspeaker | grep GLIBC
# Must show only GLIBC_2.17 and GLIBC_2.23 -- nothing higher
```

### Option 2: musl static builds (simplest, no GLIBC dependency)

Use the `dockcross/linux-armv7-musl` image for fully-static binaries. No GLIBC at all.

```bash
docker run --rm dockcross/linux-armv7-musl > /tmp/dockcross-armv7-musl
chmod +x /tmp/dockcross-armv7-musl
/tmp/dockcross-armv7-musl bash -c \
  "cd /app && ${CC} --static src/main_simple.c -o build/myapp"
```

Larger binaries, but guaranteed to run on any ARMv7 Linux regardless of libc version.

### Restore the meson.build files

The meson build files were moved to `hold/` during refactoring. Restore them and extend
for the current source layout in `src/`:

```bash
cp hold/meson.build meson.build
# Then edit src/meson.build to list all .cpp files under src/:
#   app.cpp, config.cpp, evinput.cpp, leds.cpp,
#   state/state.cpp, state/sleeping.cpp, state/listening.cpp,
#   state/processing.cpp, state/saying.cpp, state/config.cpp, state/disabled.cpp,
#   audio/audioinput.cpp, audio/audioplayer.cpp, audio/audiovolume.cpp, audio/wakeword.cpp,
#   audio/alsa/audiofifo.cpp, audio/alsa/input.cpp, audio/alsa/pa_ringbuffer.c, audio/alsa/volume.cpp,
#   audio/pulseaudio/input.cpp, audio/pulseaudio/volume.cpp
# Add alsa to deps: dependency('alsa')
```

### Deploy to device

```bash
# Push binary
adb push builddir/src/smartspeaker /opt/ss/bin/
adb push config.ini /opt/ss/

# If using dynamic glib from device (/usr/lib/libglib-2.0.so.0 is glib 2.50.1):
adb shell "export LD_LIBRARY_PATH=/usr/lib:/lib; /opt/ss/bin/smartspeaker"

# If you built with your own glib .so files:
adb push lib/*.so /opt/ss/lib/
adb shell "export LD_LIBRARY_PATH=/opt/ss/lib:/usr/lib:/lib; /opt/ss/bin/smartspeaker"
```

### Take over from the Baidu/Genie processes

On boot the device runs `/opt/duer/dcslaunch.sh` → `genie-client`.

To run your code instead for a single session:

```bash
adb shell "kill \$(pidof genie-client); /opt/ss/bin/smartspeaker"
```

To persist across reboots, the most reliable approach is overriding the launch script
(since `/opt` is writable):

```bash
adb shell "mv /opt/duer/dcslaunch.sh /opt/duer/dcslaunch.sh.orig"
adb push my_launch.sh /opt/duer/dcslaunch.sh
adb shell "chmod +x /opt/duer/dcslaunch.sh"
```

---

## Track B: Custom OS via OpenWRT (A/B partition safe update)

The device has A/B boot slots (`bootA`/`bootB`, `rootfsA`/`rootfsB`). The currently active
set is A. You can write a new OS to the B slot and switch to it; if it fails to boot, the
device sits idle until you power-cycle, which reverts to A (U-Boot env `boot_partition=bootA`
is unchanged until you explicitly set it).

### 1. Build OpenWRT for sunxi/cortexa7

```bash
git clone https://github.com/openwrt/openwrt.git ~/Code2/openwrt
cd ~/Code2/openwrt
./scripts/feeds update -a && ./scripts/feeds install -a
make menuconfig
# Target System: Allwinner (sunxi)
# Subtarget: Cortex-A7 (cortexa7)
# Add new board using device-tree.dts from this repo
make download
make -j$(nproc)
```

The device tree is at `device-tree.dts` in this repo (extracted from the running device via
`adb pull -a /proc/device-tree && dtc -I fs -O dts`). Place it under
`target/linux/sunxi/files-*/arch/arm/boot/dts/sun8iw18p1-baidu-xiaodu.dts`
and add a board profile in `target/linux/sunxi/image/`.

Build outputs go to `bin/targets/sunxi/cortexa7/`:
- `openwrt-sunxi-cortexa7-*-squashfs-sdcard.img.gz` — root FS
- Kernel image: `build_dir/target-*/linux-sunxi_cortexa7/`

### 2. Write to inactive partition (from device shell)

```bash
# On device: back up current rootfsB first
dd if=/dev/by-name/rootfsB of=/tmp/rootfsB.backup.bin

# Transfer new rootfs to device (from host):
adb push openwrt-squashfs.img /tmp/new_rootfs.img

# Flash to rootfsB
dd if=/tmp/new_rootfs.img of=/dev/by-name/rootfsB bs=4096

# Flash new kernel to bootB
dd if=/tmp/new_kernel.bin of=/dev/by-name/bootB bs=4096

# Tell U-Boot to try B next time
fw_setenv boot_partition bootB
reboot
```

### 3. Verify / roll back

After reboot, SSH in or check ADB. If the new OS works, leave `boot_partition=bootB`.
If it doesn't boot, power-cycle -- U-Boot falls back to bootA automatically (it still has
`rootfsA` untouched).

To explicitly roll back:

```bash
fw_setenv boot_partition bootA
reboot
```

---

## Quick Reference

```bash
# Open device shell
adb shell

# Push a file to device
adb push <local> <remote>

# Pull a file from device
adb pull <remote> <local>

# Build inside Docker (after fixing Dockerfile.dockcross)
docker build -f Dockerfile.dockcross -t smartspeaker-cross .
docker run --rm -v $(pwd):/app smartspeaker-cross bash -c \
  "meson setup --wipe --cross-file=scripts/cross_file.txt builddir && ninja -C builddir"

# Build a static test binary directly
docker run --rm -v $(pwd):/app arm32v7-cross-compiler \
  bash -c '${CC} --static /app/test/helloworld.c -o /app/build/helloworld'

# Check GLIBC requirements of an ARM binary
arm-linux-gnueabihf-objdump -p <binary> | grep GLIBC

# Run binary on device
adb shell "export LD_LIBRARY_PATH=/usr/lib:/lib; /opt/ss/bin/smartspeaker"

# Kill Genie client
adb shell "kill \$(pidof genie-client)"

# Check U-Boot env (boot slot, etc.)
adb shell fw_printenv

# Switch boot partition
adb shell "fw_setenv boot_partition bootB"

# Back up NAND flash (93 MB, slow)
adb shell "dd if=/dev/nand0 of=/tmp/nand_backup.bin"
```
