# Sourced by the scripts that talk to a unit over adb, to choose the unit.
# adb uses the unit whose serial is in $ANDROID_SERIAL. $UNIT, the last six hex
# digits of a unit's WiFi MAC (its name, e.g. 1486a2), selects that unit
# instead. Without either, a single attached unit is used; with more than one,
# this stops and lists them (serial and WiFi MAC), so the command can be rerun
# as  UNIT=<MAC suffix> scripts/<script> ...  or  ANDROID_SERIAL=<serial> ...
units() { adb devices | awk 'NR > 1 && $2 == "device" {print $1}'; }
mac() { timeout 5 adb -s "$1" shell cat /sys/class/net/wlan0/address </dev/null | tr -d '\r'; }
if [ -n "${UNIT:-}" ]; then
	want=$(echo "$UNIT" | tr 'A-F' 'a-f')
	ANDROID_SERIAL=
	for s in $(units); do
		case "$(mac "$s" | tr -d :)" in *"$want") ANDROID_SERIAL=$s ;; esac
	done
	[ -n "$ANDROID_SERIAL" ] || { echo "unit $UNIT isn't on adb (adb devices)" >&2; exit 1; }
	export ANDROID_SERIAL
	unset UNIT
elif [ -z "${ANDROID_SERIAL:-}" ] && [ "$(units | wc -l)" -gt 1 ]; then
	echo "more than one unit on adb; choose one with UNIT=<MAC suffix> or ANDROID_SERIAL=<serial>:" >&2
	for s in $(units); do
		echo "  $s  $(mac "$s")" >&2
	done
	exit 1
fi
adb get-state >/dev/null || exit 1
