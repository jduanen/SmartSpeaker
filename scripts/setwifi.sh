#!/bin/bash
#
# Script to set the WiFi credentials for a smart speaker connected via USB with adb
#
# Usage: setwifi.sh <ssid> <passwd>

SSID=$1
PASSWD=$2

! adb shell "cat > /data/wifi/wpa_supplicant.conf <<'EOF'
ctrl_interface=/var/run/wpa_supplicant
update_config=1
network={
    scan_ssid=1
    key_mgmt=WPA-PSK
    ssid=\"${SSID}\"
    psk=\"${PASSWD}\"
}
EOF
wpa_cli -i wlan0 reconfigure"

