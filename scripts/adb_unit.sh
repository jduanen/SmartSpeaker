# Sourced by the scripts that talk to a unit over adb, to choose the unit.
# adb uses the unit whose serial is in $ANDROID_SERIAL. Without it, a single
# attached unit is used; with more than one, this stops and lists them (serial
# and WiFi MAC, whose last six digits name the unit, e.g. 1486a2), so the
# command can be rerun as  ANDROID_SERIAL=<serial> scripts/<script> ...
if [ -z "${ANDROID_SERIAL:-}" ] && [ "$(adb devices | awk 'NR > 1 && $2 == "device"' | wc -l)" -gt 1 ]; then
	echo "more than one unit on adb; choose one with ANDROID_SERIAL=<serial>:" >&2
	for s in $(adb devices | awk 'NR > 1 && $2 == "device" {print $1}'); do
		echo "  $s  $(timeout 5 adb -s "$s" shell cat /sys/class/net/wlan0/address </dev/null | tr -d '\r')" >&2
	done
	exit 1
fi
adb get-state >/dev/null || exit 1
