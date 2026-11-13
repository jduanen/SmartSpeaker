#!/bin/sh
# Run esphome in the baidu-esphome image. The armhf / GLIBC 2.23 cross
# compiler is selected by the baidu_host component itself.
# Run from baidu/. Usage: scripts/esphome.sh compile|upload|logs|run baidu-xdh-1d-a1.yaml ...
set -eu

exec docker run --rm --network host -u "$(id -u):$(id -g)" \
  -e HOME=/config/.esphome/home -v "$(pwd)":/config \
  baidu-esphome "$@"
