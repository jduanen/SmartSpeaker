#!/bin/bash
#
# Script to build and install the ESPHome firmware for a Baidu device
#
# N.B. Edit the configs here, not in the builder

# after a config change, rerun `addon/prepare.sh`` and copy the result to
#  the HA Server's '/config/esphome' directory
#### FIXME determine if the config changed and update it on the HA server -- rsync?

#### FIXME determine the correct postfix to use for the name
MAC_POSTFIX="143a00"
BAIDU_NAME="baidu-${MAC_POSTFIX}"

# run from baidu/:
scripts/esphome.sh compile ${BAIDU_NAME}.yaml
scripts/esphome.sh upload ${BAIDU_NAME}.yaml --device ${BAIDU_NAME}.local
