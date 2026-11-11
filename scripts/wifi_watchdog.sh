#!/bin/sh
# WiFi watchdog for the Xiaodu's RTL8723DS. Started by dcslaunch.sh.
#
# Failure seen on this unit: the station stays associated and its own
# outgoing traffic works, but it stops receiving broadcast/group frames
# (likely a failed WPA group-key rekey). Peers can then no longer ARP the
# unit, so TCP from HA stalls for 7-15 s per connect. A fresh association
# fixes it.
#
# Check: TCP connect to HA (the peer of the ESPHome API connection; the
# last one seen is remembered) or, before HA has connected, to the default
# gateway. Any answer, even "connection refused", proves the peer can reach
# us; only a timeout counts. After WD_FAILS timeouts in a row, reassociate.

WD_INTERVAL=${WD_INTERVAL:-60}   # seconds between checks
WD_FAILS=${WD_FAILS:-3}          # consecutive timeouts before acting
WD_PORT=${WD_PORT:-8123}         # HA's HTTP port; a refusal also counts as reachable
LOG=${WD_LOG:-/tmp/smartspeaker.log}

log() { echo "wifi_watchdog: $*" >>"$LOG"; }

peer=${WD_TARGET:-}
fails=0
while true; do
	sleep "$WD_INTERVAL"

	if [ "$(wpa_cli -i wlan0 status 2>/dev/null | grep wpa_state=)" != "wpa_state=COMPLETED" ]; then
		fails=0 # wpa_supplicant is already (re)connecting
		continue
	fi

	if [ -z "$WD_TARGET" ]; then
		ha=$(netstat -tn 2>/dev/null | awk '$4 ~ /:6053$/ && $6 == "ESTABLISHED" {split($5, a, ":"); print a[1]; exit}')
		[ -n "$ha" ] && peer=$ha
	fi
	target=${peer:-$(ip route | awk '/^default/ {print $3; exit}')}
	port=$WD_PORT
	[ -z "$peer" ] && port=80
	[ -z "$target" ] && continue

	curl -s -o /dev/null --connect-timeout 5 "http://$target:$port/"
	if [ $? -eq 28 ]; then
		fails=$((fails + 1))
		log "no TCP answer from $target:$port ($fails/$WD_FAILS)"
		if [ "$fails" -ge "$WD_FAILS" ]; then
			log "reassociating wlan0"
			wpa_cli -i wlan0 reassociate >/dev/null
			fails=0
		fi
	else
		[ "$fails" -gt 0 ] && log "$target:$port answering again"
		fails=0
	fi
done
