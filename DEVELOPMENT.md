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

The device has **GLIBC 2.23**. Using a modern Ubuntu cross-compiler naively produces binaries
that require GLIBC 2.28–2.34 due to three separate root causes, each needing its own fix.

#### Root causes and fixes

**1. CRT startup files come from the cross-compiler, not the sysroot**

When GCC links a binary it injects `Scrt1.o` / `crti.o` (C runtime startup) by *absolute
path* from its own installation (e.g. `/usr/arm-linux-gnueabihf/lib/Scrt1.o`). This happens
even when `--sysroot` is set, because GCC bypasses the sysroot for its own support files.
Ubuntu 22.04's `Scrt1.o` calls `__libc_start_main@@GLIBC_2.34`.

Fix: `-B/sysroot/usr/lib/arm-linux-gnueabihf` in link args tells GCC to look there first for
CRT objects, picking up the xenial versions that call `__libc_start_main@@GLIBC_2.4`.

**2. Ubuntu 22.04 headers redirect standard calls to 64-bit variants**

When `_FILE_OFFSET_BITS=64` is defined (meson default), Ubuntu 22.04's headers `#define
fcntl fcntl64` and `stat → stat64`. The target's GLIBC 2.23 doesn't export `fcntl64` or
`stat64` as separate versioned symbols — they were added in GLIBC 2.28 and 2.33.

Fix: `src/glibc_compat.c` provides `fcntl64`, `stat64`, `lstat64`, and `fstat64` using
direct ARM syscalls (numbers 221, 195–197) so the call goes straight to the kernel, bypassing
the missing GLIBC wrapper entirely.

**3. GCC 11 static libstdc++ references a GLIBC 2.32 variable**

`-static-libstdc++` pulls in GCC 11's `libstdc++.a`, which references `__libc_single_threaded`
(added in GLIBC 2.32) as a per-thread lock-elision hint. GLIBC 2.23 doesn't export it.

Fix: `src/glibc_compat.c` also defines `char __libc_single_threaded = 0` (always
multi-threaded → locks are never elided, which is the safe default).

#### How the Docker image achieves this

`Dockerfile.dockcross` uses a two-stage build:

- **Stage 1** — Ubuntu 16.04 (xenial, GLIBC 2.23) installs all armhf `-dev` packages.
  This gives us headers, `.so` stubs, and — critically — the xenial CRT objects (`crt1.o`,
  `Scrt1.o`, `crti.o`).
- **Stage 2** — Ubuntu 22.04 runs GCC 11 (`arm-linux-gnueabihf-g++`) with `--sysroot`
  pointing at the xenial libraries copied in from Stage 1.

A post-COPY step rewrites all absolute symlinks inside `/sysroot` (e.g. `libglib-2.0.so →
/lib/arm-linux-gnueabihf/libglib-2.0.so.0`) to be sysroot-prefixed, so the host linker
follows them into the sysroot rather than looking them up on the build host.

### Build

```bash
# Build the Docker image (one-time; ~5 min)
docker build -f Dockerfile.dockcross -t smartspeaker-cross .

# Cross-compile
docker run --rm -v $(pwd):/app smartspeaker-cross bash -c \
  "meson setup --wipe --cross-file=scripts/cross_file.txt builddir && ninja -C builddir"
```

The output binary is `builddir/src/smartspeaker`.

### Verify GLIBC requirements

```bash
docker run --rm -v $(pwd):/app smartspeaker-cross \
  arm-linux-gnueabihf-objdump -p builddir/src/smartspeaker | grep GLIBC
# Expected: only GLIBC_2.4, GLIBC_2.7, GLIBC_2.17  (all ≤ 2.23)
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

# Build Docker image (one-time)
docker build -f Dockerfile.dockcross -t smartspeaker-cross .

# Cross-compile
docker run --rm -v $(pwd):/app smartspeaker-cross bash -c \
  "meson setup --wipe --cross-file=scripts/cross_file.txt builddir && ninja -C builddir"

# Check GLIBC requirements of an ARM binary (must be ≤ 2.23)
docker run --rm -v $(pwd):/app smartspeaker-cross \
  arm-linux-gnueabihf-objdump -p builddir/src/smartspeaker | grep GLIBC

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
