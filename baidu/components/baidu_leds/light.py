"""The Xiaodu's 12-LED ring as an ESPHome addressable light.

The ring's driver exposes it in sysfs: writing 1 to `user_space` takes it from
the kernel's own effects, and `set_all_led_color` takes twelve decimal 0xRRGGBB
values. Ported from the C++ app's src/leds.cpp.
"""

import esphome.codegen as cg
from esphome.components import light
import esphome.config_validation as cv
from esphome.const import CONF_NUM_LEDS, CONF_OUTPUT_ID, CONF_PATH

baidu_leds_ns = cg.esphome_ns.namespace("baidu_leds")
BaiduLeds = baidu_leds_ns.class_("BaiduLeds", light.AddressableLight)

CONFIG_SCHEMA = cv.All(
    light.ADDRESSABLE_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(BaiduLeds),
            cv.Optional(CONF_PATH, default="/sys/class/leds/isled01"): cv.string,
            cv.Optional(CONF_NUM_LEDS, default=12): cv.int_range(min=1, max=36),
        }
    ),
    cv.only_on(["host"]),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID], config[CONF_NUM_LEDS])
    cg.add(var.set_path(config[CONF_PATH]))
    await light.register_light(var, config)
    await cg.register_component(var, config)
