#!/bin/sh
#
# Build the ESPHome firmware (baidu) for one or more Baidu speakers, install it
# on each over WiFi (ESPHome OTA), then copy the speakers' configs to the HA
# server so its ESPHome Device Builder lists them.
# Usage: scripts/installBaidu.sh <unit>...   (from any directory)
#   unit: the last six hex digits of the speaker's WiFi MAC, i.e. the one in
#   baidu/baidu-<unit>.yaml, e.g. scripts/installBaidu.sh 143a00 1486a2
#
# N.B. Edit the configs in baidu/, not in the builder: the copy on HA is
# replaced. The speaker must already run baidu (first install: baidu/README.md,
# "Install on a unit").
#
# The configs go over SSH with rsync to $HA_SSH (default homeassistant.local,
# with the user and key from ~/.ssh/config), the HA server's SSH add-on, which
# needs this machine's public key and rsync (see baidu/README.md); its user needs
# passwordless sudo (the add-on's non-root user has it). Only /config/esphome/baidu/
# (the shared package and components) and /config/esphome/baidu-*.yaml are
# written; other devices' configs and secrets.yaml are left alone, so add a new
# speaker's baidu_<unit>_api_key to the HA server's secrets.yaml yourself.
# HA_SSH= (empty) skips the copy.
set -eu
cd "$(dirname "$0")/../baidu"
HA_SSH=${HA_SSH-homeassistant.local}

if [ $# -eq 0 ]; then
	echo "usage: scripts/installBaidu.sh <unit>...   units:" $(ls baidu-*.yaml | sed -n 's/^baidu-\([0-9a-f]\{6\}\)\.yaml$/\1/p')
	exit 1
fi
for u; do
	[ -f "baidu-$u.yaml" ] || { echo "no baidu/baidu-$u.yaml"; exit 1; }
done

for u; do
	scripts/esphome.sh compile "baidu-$u.yaml"
	# upload sends the last build; it doesn't compile
	scripts/esphome.sh upload "baidu-$u.yaml" --device "baidu-$u.local"
done

if [ -n "$HA_SSH" ]; then
	addon/prepare.sh >/dev/null
	E=.esphome/addon/esphome
	# -c: compare contents (prepare.sh rewrites every file); -i: list what changed
	# sudo: /config/esphome belongs to root; __pycache__ is the builder's own
	R='sudo rsync'
	rsync --rsync-path="$R" -rci --delete --exclude __pycache__ "$E/baidu/" "$HA_SSH:/config/esphome/baidu/"
	rsync --rsync-path="$R" -ci "$E"/baidu-*.yaml "$HA_SSH:/config/esphome/"
	echo "configs copied to $HA_SSH:/config/esphome"
fi
