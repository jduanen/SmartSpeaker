"""mDNS responder for the host platform.

ESPHome's mdns component builds its service records on host but sends nothing
(mdns_host.cpp). This answers queries for those records and announces them,
so Home Assistant discovers the device as it does an ESP32.
"""

import esphome.codegen as cg
from esphome.components import mdns
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["mdns"]

CONF_MDNS_ID = "mdns_id"

baidu_mdns_ns = cg.esphome_ns.namespace("baidu_mdns")
BaiduMdns = baidu_mdns_ns.class_("BaiduMdns", cg.Component)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BaiduMdns),
            cv.GenerateID(CONF_MDNS_ID): cv.use_id(mdns.MDNSComponent),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on(["host"]),
)


async def to_code(config):
    # keep the compiled services after MDNSComponent::setup() for get_services()
    mdns.enable_mdns_storage()
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_mdns(await cg.get_variable(config[CONF_MDNS_ID])))
