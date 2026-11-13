#!/bin/sh
# baidu's launcher, installed as /opt/baidu/bin/dcslaunch.sh by
# baidu/scripts/install.sh. The stock /etc/init.d/rc.final runs
# /opt/duer/dcslaunch.sh at boot, which install.sh points here.
# Runs the ESPHome program in a respawn loop. ESPHome OTA replaces
# /opt/baidu/bin/baidu and re-execs it in place (same PID), so the loop only
# matters when the program exits (crash, or HA's restart).
cd /opt/baidu
export TZ=UTC
export LD_LIBRARY_PATH=/lib:/usr/lib
export ESPHOME_PREFDIR=/opt/baidu/prefs
LOG=/tmp/baidu.log

# legacy_rates in etc/launcher.conf (default true): reload the WiFi driver as an
# 802.11b-only client. The RTL8723DS here often fails to receive OFDM/11n
# frames (stalls, deauth reason 34) while the CCK rates get through; about
# 4 Mbps, steady. Same as the C++ app's [wifi] legacy_rates (see its README).
legacy=$(sed -n 's/^legacy_rates *= *//p' etc/launcher.conf 2>/dev/null)
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
# mic preamp step 3 = 30 dB, as the C++ app (baidu_mic doesn't set gains);
# the speaker volume is restored by baidu_speaker from the preferences
amixer -D hw:audiocodec cset name='MIC1 gain volume' 3 >/dev/null
amixer -D hw:audiocodec cset name='MIC2 gain volume' 3 >/dev/null

# reassociates WiFi when peers stop reaching us (see the script)
/opt/baidu/bin/wifi_watchdog.sh &

# /tmp is RAM: keep at most two ~1 MB logs. The program keeps the log open in
# append mode, so copy and truncate it in place rather than renaming it.
(while true; do
	sleep 60
	if [ "$(wc -c <"$LOG" 2>/dev/null || echo 0)" -gt 1048576 ]; then
		cp "$LOG" "$LOG.1"
		: >"$LOG"
	fi
done) &

while true; do
	/opt/baidu/bin/baidu >>"$LOG" 2>&1
	echo "baidu exited ($?), restarting" >>"$LOG"
	sleep 2
done
