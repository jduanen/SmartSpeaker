#!/bin/sh
# Undo scripts/install.sh: put back the launcher that was there before baidu
# (DuerOS's, Genie's or the C++ app's), or remove the hook if there was none,
# and restore anything listed in /opt/baidu/etc/disabled.txt.
# Usage: scripts/uninstall.sh [--purge]   (--purge also removes /opt/baidu,
#        including the preferences and the API key HA provisioned)
set -eu

sh_() { adb shell "$1" </dev/null | tr -d '\r'; }

if [ "$(sh_ '[ -f /opt/baidu/etc/dcslaunch.sh.prev ] && echo y')" = y ]; then
	sh_ "cp -p /opt/baidu/etc/dcslaunch.sh.prev /opt/duer/dcslaunch.sh && rm /opt/baidu/etc/dcslaunch.sh.prev && echo restored the previous dcslaunch.sh"
else
	# no launcher before baidu: remove the hook (only if it is ours)
	sh_ "grep -q /opt/baidu/bin /opt/duer/dcslaunch.sh 2>/dev/null && rm /opt/duer/dcslaunch.sh && echo removed dcslaunch.sh; rmdir /opt/duer 2>/dev/null; true"
fi
sh_ "[ -f /opt/baidu/etc/disabled.txt ] && while read p; do mv \$p.orig \$p && echo restored \$p; done </opt/baidu/etc/disabled.txt && rm /opt/baidu/etc/disabled.txt; true"
if [ "${1:-}" = --purge ]; then
	sh_ "rm -rf /opt/baidu"
	echo "removed /opt/baidu"
fi

echo "done; power-cycle the unit (or adb reboot)"
