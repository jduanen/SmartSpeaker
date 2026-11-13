"""A key on a Linux input device (/dev/input/eventN) as a binary sensor.

On while the key is held, so on_press, on_click and on_multi_click work as on
a GPIO button. Each sensor opens the device itself (evdev gives every reader
every event). Ported from the C++ app's src/evinput.cpp.
"""

import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import CONF_KEY

CONF_DEVICE = "device"

evdev_ns = cg.esphome_ns.namespace("evdev")
EvdevKey = evdev_ns.class_("EvdevKey", binary_sensor.BinarySensor, cg.Component)

# the Xiaodu's keys (sunxi-keyboard); any other code can be given as a number
KEYS = {
    "KEY_MUTE": 113,
    "KEY_VOLUMEDOWN": 114,
    "KEY_VOLUMEUP": 115,
    "KEY_PLAYPAUSE": 164,
}


def _key(value):
    if isinstance(value, str) and value.upper() in KEYS:
        return KEYS[value.upper()]
    return cv.int_range(min=1, max=767)(value)


CONFIG_SCHEMA = cv.All(
    binary_sensor.binary_sensor_schema(EvdevKey)
    .extend(
        {
            cv.Optional(CONF_DEVICE, default="/dev/input/event0"): cv.string,
            cv.Required(CONF_KEY): _key,
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    cv.only_on(["host"]),
)


async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)
    await cg.register_component(var, config)
    cg.add(var.set_device(config[CONF_DEVICE]))
    cg.add(var.set_key(config[CONF_KEY]))
