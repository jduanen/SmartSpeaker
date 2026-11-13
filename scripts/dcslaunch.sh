#!/bin/sh
# Installed as /opt/duer/dcslaunch.sh by scripts/install.sh, replacing the
# DuerOS launcher (kept as dcslaunch.sh.orig). Runs smartspeaker in a respawn loop.
cd /opt/ss/etc
export TZ=UTC
export LD_LIBRARY_PATH=/lib:/usr/lib

# [wifi] legacy_rates in config.ini (default true): reload the WiFi driver as an
# 802.11b-only client. The RTL8723DS here often fails to receive OFDM/11n
# frames (stalls, deauth reason 34) while the CCK rates get through; about
# 4 Mbps, steady. See README, "Stock (DuerOS) units".
legacy=$(sed -n '/^\[wifi\]/,/^\[/s/^legacy_rates *= *//p' config.ini)
if [ "${legacy:-true}" = true ] &&
	[ "$(cat /sys/module/8723ds/parameters/rtw_wireless_mode 2>/dev/null)" != 1 ]; then
	killall wpa_supplicant udhcpc 2>/dev/null
	sleep 1
	ifconfig wlan0 down
	rmmod 8723ds && insmod /lib/modules/$(uname -r)/8723ds.ko rtw_wireless_mode=1
	sleep 2
	ifconfig wlan0 up
	wpa_supplicant -B -Dnl80211 -i wlan0 -s -c /data/wifi/wpa_supplicant.conf
	i=0
	while [ $i -lt 30 ] && ! wpa_cli -i wlan0 status 2>/dev/null | grep -q "wpa_state=COMPLETED"; do
		sleep 1
		i=$((i + 1))
	done
	# the reload killed the DHCP client. The stock S42wifi may still be in its
	# 20 s wait for WiFi and start one itself; otherwise start one here
	sleep 2
	[ -n "$(pidof udhcpc)" ] ||
		udhcpc -i wlan0 -x hostname:Xiaodu-AudioSpeaker >/dev/null 2>&1 &
fi

# wait for WiFi (NTP, mDNS and HA need it); start anyway after 60 s
i=0
while [ $i -lt 60 ] && ! ifconfig wlan0 2>/dev/null | grep -q "inet addr"; do
	sleep 1
	i=$((i + 1))
done

if [ -z "$(pidof ntpd)" ]; then
	/usr/bin/start_ntp.sh &
fi
amixer -D hw:audiocodec cset name='LINEOUT volume' 4 > /dev/null
# mic preamp step 3 = 30 dB (Genie used 6 = 39 dB, which compresses from
# ~70% volume and clips the mics above ~85%; see README Phase 6)
amixer -D hw:audiocodec cset name='MIC1 gain volume' 3 > /dev/null
amixer -D hw:audiocodec cset name='MIC2 gain volume' 3 > /dev/null

# reassociates WiFi when peers stop reaching us (see the script)
/opt/ss/bin/wifi_watchdog.sh &

while true; do
	/opt/ss/bin/smartspeaker >>/tmp/smartspeaker.log 2>&1
	echo "smartspeaker exited ($?), restarting" >>/tmp/smartspeaker.log
	sleep 2
done
