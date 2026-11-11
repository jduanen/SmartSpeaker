#!/bin/sh
# Inventory a stock Xiaodu unit over adb and record the results.
# Read-only on the device. Usage: scripts/probe.sh [outfile]   (default notes/stock.txt)
set -eu

OUT=${1:-notes/stock.txt}
adb get-state >/dev/null

run() {
  printf '\n### %s\n' "$1" >>"$OUT"
  adb shell "$1" 2>&1 | tr -d '\r' >>"$OUT" || true
}

{
  echo "# Stock unit probe"
  echo "# date: $(date -u +%Y-%m-%dT%H:%M:%SZ)  serial: $(adb get-serialno)"
} >"$OUT"

# Inventory
run 'cat /etc/openwrt_release /etc/os-release 2>/dev/null; uname -a'
run 'ps w'
run 'cat /opt/duer/dcslaunch.sh'
run 'ls -lR /opt/duer | head -100'
run 'df'
run 'free'
run 'ls /etc/init.d /etc/rc.d'
run 'cat /etc/inittab'
run 'cat /proc/asound/cards /proc/asound/pcm'
run 'amixer -D hw:audiocodec contents'
run 'cat /proc/bus/input/devices'
run 'ls /sys/class/leds/'
run 'ls /usr/lib | grep -E "curl|speex|glib|gio|asound|ssl|stdc"'
run 'fw_printenv'

# OTA updater / watchdog
run 'ls -l /dev/watchdog* 2>&1; cat /proc/sys/kernel/watchdog 2>&1'
run 'ls /etc/crontabs 2>&1; cat /etc/crontabs/* 2>&1'
run 'find /opt/duer /usr/bin /usr/sbin /etc -iname "*ota*" -o -iname "*upgrade*" -o -iname "*update*" -o -iname "*swupdate*" -o -iname "*watchdog*" 2>/dev/null'
run 'grep -l -i -E "ota|upgrade|swupdate" /opt/duer/*.sh /etc/init.d/* 2>/dev/null'

# LEDs: is the driver in kernel or user mode?
run 'for d in /sys/class/leds/*; do echo "== $d"; ls $d; cat $d/user_space 2>/dev/null; done'

# Keys
run 'ls -l /dev/input/'

# Capture: channel count and whether a loopback/reference channel exists
run 'arecord -D hw:audiocodec --dump-hw-params -d 1 -f S16_LE -r 48000 /dev/null 2>&1'
run 'aplay -L 2>&1; arecord -L 2>&1'
run 'cat /etc/asound.conf 2>&1'

# Storage
run 'df -h /opt /data /tmp'
run 'ls -l /dev/by-name/; cat /proc/partitions'
run 'cat /data/wifi/wpa_supplicant.conf | sed "s/psk=.*/psk=<redacted>/"'

echo "wrote $OUT"
