#!/bin/sh
# Installed as /opt/duer/dcslaunch.sh by scripts/install.sh, replacing the
# DuerOS launcher (kept as dcslaunch.sh.orig). Runs smartspeaker in a respawn loop.
cd /opt/ss/etc
export TZ=UTC
export LD_LIBRARY_PATH=/lib:/usr/lib

if [ -z "$(pidof ntpd)" ]; then
	/usr/bin/start_ntp.sh &
fi
amixer -D hw:audiocodec cset name='LINEOUT volume' 4 > /dev/null
amixer -D hw:audiocodec cset name='MIC1 gain volume' 6.5 > /dev/null
amixer -D hw:audiocodec cset name='MIC2 gain volume' 6.5 > /dev/null

while true; do
	/opt/ss/bin/smartspeaker >>/tmp/smartspeaker.log 2>&1
	echo "smartspeaker exited ($?), restarting" >>/tmp/smartspeaker.log
	sleep 2
done
