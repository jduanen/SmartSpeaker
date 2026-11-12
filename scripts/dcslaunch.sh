#!/bin/sh
# Installed as /opt/duer/dcslaunch.sh by scripts/install.sh, replacing the
# DuerOS launcher (kept as dcslaunch.sh.orig). Runs smartspeaker in a respawn loop.
cd /opt/ss/etc
export TZ=UTC
export LD_LIBRARY_PATH=/lib:/usr/lib

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
