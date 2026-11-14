#!/bin/sh
#
# Build the C++ app (smartspeaker), install it on a Xiaodu unit over adb, and
# restart it there (the launcher respawns it; no reboot needed).
# Usage: scripts/installXiaodu.sh [unit]   (from any directory)
#   unit: the last six hex digits of the unit's WiFi MAC, e.g. 1486a2; not
#   needed when only one unit is on adb (see scripts/adb_unit.sh)
# The first install on a unit is scripts/install.sh plus a power-cycle (see
# README.md, install guide); this is for updates.
set -eu
cd "$(dirname "$0")/.."

if [ $# -gt 0 ]; then
	UNIT=$1
	export UNIT
fi
. scripts/adb_unit.sh

# build and strip (builddir belongs to root, as the container runs as root)
docker run --rm -v "$(pwd)":/app smartspeaker-cross bash -c \
	"{ [ -f builddir/build.ninja ] || meson setup --cross-file=scripts/cross_file.txt builddir; } && \
	 ninja -C builddir && \
	 arm-linux-gnueabihf-strip -o builddir/src/smartspeaker.stripped builddir/src/smartspeaker"

old=$(adb shell pidof smartspeaker </dev/null | tr -d '\r')
[ -n "$old" ] || { echo "smartspeaker isn't running on $(adb get-serialno); install it with scripts/install.sh first"; exit 1; }
scripts/install.sh builddir/src/smartspeaker.stripped

# restart: the launcher starts the new build when the old one exits
adb shell "kill $old" </dev/null
i=0
while :; do
	new=$(adb shell pidof smartspeaker </dev/null | tr -d '\r')
	[ -n "$new" ] && [ "$new" != "$old" ] && break
	i=$((i + 1))
	[ $i -lt 30 ] || { echo "smartspeaker didn't restart within 30 s; check /tmp/smartspeaker.log"; exit 1; }
	sleep 1
done
echo "smartspeaker restarted on $(adb get-serialno) (pid $old -> $new)"
