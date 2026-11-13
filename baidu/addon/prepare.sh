#!/bin/sh
# Assemble what goes onto the Home Assistant host (Samba or SSH add-on):
#   <out>/addons/esphome_baidu   the add-on (the Docker build context): the files
#                                in baidu/addon plus baidu/third_party
#                                -> copy to the HA host's /addons/esphome_baidu
#   <out>/esphome                the speakers' configs for the Device Builder:
#                                baidu/ (the shared package and the components)
#                                and one baidu-<unit>.yaml per speaker
#                                -> copy into the HA host's /config/esphome
#                                   (add ota_password to its secrets.yaml)
# Run from baidu/. Usage: addon/prepare.sh [out]   (default .esphome/addon)
set -eu
OUT=${1:-.esphome/addon}

A=$OUT/addons/esphome_baidu
rm -rf "$A"
mkdir -p "$A"
cp addon/Dockerfile addon/config.yaml addon/build.yaml addon/DOCS.md "$A/"
cp -r third_party "$A/third_party"

E=$OUT/esphome
rm -rf "$E"
mkdir -p "$E/baidu"
cp baidu-xdh-1d-a1.yaml "$E/baidu/"
cp -r components "$E/baidu/components"
find "$E/baidu/components" -name __pycache__ -prune -exec rm -rf {} +
# the per-speaker files, with the package one folder down
for f in baidu-*.yaml; do
	[ "$f" = baidu-xdh-1d-a1.yaml ] && continue
	sed 's|!include baidu-xdh-1d-a1.yaml|!include baidu/baidu-xdh-1d-a1.yaml|; s|^substitutions:|substitutions:\n  baidu_dir: baidu|' \
		"$f" >"$E/$f"
done

echo "add-on:  $A  -> /addons/esphome_baidu"
echo "configs: $E  -> /config/esphome (contents)"
