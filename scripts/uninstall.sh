#!/bin/sh
# Restore DuerOS on a unit taken over by scripts/install.sh.
# Usage: scripts/uninstall.sh [--purge]   (--purge also removes /opt/ss)
set -eu

sh_() { adb shell "$1" | tr -d '\r'; }

if [ "$(sh_ '[ -f /opt/duer/dcslaunch.sh.orig ] && echo y')" = y ]; then
  sh_ "mv /opt/duer/dcslaunch.sh.orig /opt/duer/dcslaunch.sh && echo restored dcslaunch.sh"
else
  # blank-/opt unit: install.sh created the launcher, so remove it (only if it is ours)
  sh_ "grep -q /opt/ss/bin/smartspeaker /opt/duer/dcslaunch.sh 2>/dev/null && rm /opt/duer/dcslaunch.sh && echo removed dcslaunch.sh; rmdir /opt/duer 2>/dev/null; true"
fi
sh_ "[ -f /opt/ss/etc/disabled.txt ] && while read p; do mv \$p.orig \$p && echo restored \$p; done </opt/ss/etc/disabled.txt && rm /opt/ss/etc/disabled.txt"
if [ "${1:-}" = --purge ]; then
  sh_ "rm -rf /opt/ss"
  echo "removed /opt/ss"
fi

echo "done; reboot the unit (adb reboot) to start DuerOS"
