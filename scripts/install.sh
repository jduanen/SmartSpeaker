#!/bin/sh
# Take over a stock Xiaodu unit from DuerOS. Reversible with scripts/uninstall.sh.
# Run from the repo root on the host, with the unit on adb.
# Usage: scripts/install.sh [binary]   (default builddir/src/smartspeaker)
set -eu

BIN=${1:-builddir/src/smartspeaker}

# DuerOS OTA updater / watchdog binaries to disable (renamed to <path>.orig).
# Fill in from notes/stock.txt after running scripts/probe.sh.
OTA_DISABLE=""

SERIAL=$(adb get-serialno)
[ -d "backups/$SERIAL/duer" ] && [ -f "backups/$SERIAL/app.img" ] || {
  echo "no backup for $SERIAL; run scripts/backup.sh first"
  exit 1
}
[ -f "$BIN" ] || { echo "missing $BIN"; exit 1; }
[ -n "$OTA_DISABLE" ] || echo "warning: OTA_DISABLE is empty; DuerOS updates may still run"

sh_() { adb shell "$1" | tr -d '\r'; }

sh_ "mkdir -p /opt/ss/bin /opt/ss/share/models /opt/ss/share/sounds /opt/ss/etc"
adb push "$BIN" /opt/ss/bin/smartspeaker
sh_ "chmod +x /opt/ss/bin/smartspeaker"
adb push scripts/wifi_watchdog.sh /opt/ss/bin/wifi_watchdog.sh
sh_ "chmod +x /opt/ss/bin/wifi_watchdog.sh"
# keep an existing on-device config (it may hold the encryption key)
if [ "$(sh_ '[ -f /opt/ss/etc/config.ini ] && echo y')" != y ]; then
  adb push config.ini /opt/ss/etc/config.ini
fi
[ -d share/models ] && adb push share/models/. /opt/ss/share/models/
[ -d share/sounds ] && adb push share/sounds/. /opt/ss/share/sounds/

# launcher: keep the first .orig, never overwrite it
sh_ "[ -f /opt/duer/dcslaunch.sh.orig ] || mv /opt/duer/dcslaunch.sh /opt/duer/dcslaunch.sh.orig"
adb push scripts/dcslaunch.sh /opt/duer/dcslaunch.sh
sh_ "chmod +x /opt/duer/dcslaunch.sh"

# uninstall.sh restores everything listed in /opt/ss/etc/disabled.txt
for p in $OTA_DISABLE; do
  sh_ "[ -e $p ] && [ ! -e $p.orig ] && mv $p $p.orig && echo $p >>/opt/ss/etc/disabled.txt && echo disabled $p"
done

echo "installed; reboot the unit (adb reboot) to start smartspeaker"
