import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.components.ble_ext_scanner import (
    BLEExtScannerComponent,
    BTHomeReading,
)

DEPENDENCIES = ["mqtt", "ble_ext_scanner"]

CONF_SCANNER_ID = "scanner_id"
CONF_DISCOVERY_PREFIX = "discovery_prefix"
CONF_STATE_PREFIX = "state_prefix"
CONF_NODE_NAME = "node_name"
CONF_CONFIG_INTERVAL = "config_interval"
CONF_STATE_INTERVAL = "state_interval"

ha_discovery_ns = cg.esphome_ns.namespace("ha_discovery")
HADiscoveryComponent = ha_discovery_ns.class_("HADiscoveryComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(HADiscoveryComponent),
        cv.Required(CONF_SCANNER_ID): cv.use_id(BLEExtScannerComponent),
        cv.Optional(CONF_DISCOVERY_PREFIX, default="homeassistant"): cv.string,
        cv.Optional(CONF_STATE_PREFIX, default="bthome"): cv.string,
        cv.Optional(CONF_NODE_NAME, default=""): cv.string,
        cv.Optional(CONF_CONFIG_INTERVAL, default=300): cv.int_range(min=30, max=86400),
        cv.Optional(CONF_STATE_INTERVAL, default=10): cv.int_range(min=1, max=3600),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    scanner = await cg.get_variable(config[CONF_SCANNER_ID])
    cg.add(var.set_scanner(scanner))

    cg.add(var.set_discovery_prefix(config[CONF_DISCOVERY_PREFIX]))
    cg.add(var.set_state_prefix(config[CONF_STATE_PREFIX]))
    cg.add(var.set_node_name(config[CONF_NODE_NAME]))
    cg.add(var.set_config_interval(config[CONF_CONFIG_INTERVAL]))
    cg.add(var.set_state_interval(config[CONF_STATE_INTERVAL]))
