#!/bin/sh
# Install baidu on a Xiaodu over adb, so it starts at boot. After this, updates
# come over WiFi (ESPHome OTA: scripts/esphome.sh upload, or HA).
# Run from baidu/ on the host, with the unit on adb.
# Usage: scripts/install.sh <program>
#   e.g. scripts/install.sh .esphome/build/baidu-143a00/.pioenvs/baidu-143a00/program
#
# The stock /etc/init.d/rc.final runs /opt/duer/dcslaunch.sh at boot. This
# saves the launcher that is there (DuerOS's, Genie's or the C++ app's) as
# /opt/baidu/etc/dcslaunch.sh.prev, once, and replaces it with a two-line hook
# that runs /opt/baidu/bin/dcslaunch.sh. scripts/uninstall.sh puts it back.
set -eu

BIN=${1:?usage: scripts/install.sh <program>}

# DuerOS OTA updater / watchdog binaries to disable (renamed to <path>.orig),
# as in the C++ app's scripts/install.sh. Fill in from ../notes/stock.txt.
OTA_DISABLE=""

SERIAL=$(adb get-serialno)
[ -f "../backups/$SERIAL/app.img" ] || {
	echo "no backup for $SERIAL; run scripts/backup.sh from the repo root first"
	exit 1
}
[ -f "$BIN" ] || { echo "missing $BIN"; exit 1; }

sh_() { adb shell "$1" </dev/null | tr -d '\r'; }

sh_ "mkdir -p /opt/baidu/bin /opt/baidu/etc /opt/baidu/prefs"
# push beside the running program, then rename (a running binary can't be overwritten)
adb push "$BIN" /opt/baidu/bin/baidu.new
sh_ "chmod +x /opt/baidu/bin/baidu.new && mv /opt/baidu/bin/baidu.new /opt/baidu/bin/baidu"
adb push scripts/dcslaunch.sh /opt/baidu/bin/dcslaunch.sh
adb push scripts/wifi_watchdog.sh /opt/baidu/bin/wifi_watchdog.sh
sh_ "chmod +x /opt/baidu/bin/dcslaunch.sh /opt/baidu/bin/wifi_watchdog.sh"
# the YAML plays these as file:///opt/baidu/share/sounds/<name>.flac
sh_ "mkdir -p /opt/baidu/share/sounds"
adb push share/sounds/. /opt/baidu/share/sounds/
if [ "$(sh_ '[ -f /opt/baidu/etc/launcher.conf ] && echo y')" != y ]; then
	adb push scripts/launcher.conf /opt/baidu/etc/launcher.conf
fi

# boot hook: keep the launcher that was there (once), then point it at ours
sh_ "mkdir -p /opt/duer"
sh_ "[ -f /opt/baidu/etc/dcslaunch.sh.prev ] || [ ! -f /opt/duer/dcslaunch.sh ] || grep -q /opt/baidu/bin /opt/duer/dcslaunch.sh || cp -p /opt/duer/dcslaunch.sh /opt/baidu/etc/dcslaunch.sh.prev"
sh_ "printf '#!/bin/sh\n# baidu (baidu/scripts/install.sh); the previous launcher is /opt/baidu/etc/dcslaunch.sh.prev\nexec /opt/baidu/bin/dcslaunch.sh\n' >/opt/duer/dcslaunch.sh && chmod +x /opt/duer/dcslaunch.sh"

# uninstall.sh restores everything listed in /opt/baidu/etc/disabled.txt
for p in $OTA_DISABLE; do
	sh_ "[ -e $p ] && [ ! -e $p.orig ] && mv $p $p.orig && echo $p >>/opt/baidu/etc/disabled.txt && echo disabled $p"
done

echo "installed; power-cycle the unit (or adb reboot) to start baidu"
