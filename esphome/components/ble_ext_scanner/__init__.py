import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import CONF_ID, CONF_TRIGGER_ID
from esphome.components.esp32 import add_idf_sdkconfig_option

DEPENDENCIES = ["esp32"]

CONF_SCAN_INTERVAL = "scan_interval"
CONF_SCAN_WINDOW = "scan_window"
CONF_SCAN_CODED = "scan_coded"
CONF_SCAN_1M = "scan_1m"
CONF_ON_READING = "on_reading"

ble_ext_scanner_ns = cg.esphome_ns.namespace("ble_ext_scanner")
BLEExtScannerComponent = ble_ext_scanner_ns.class_(
    "BLEExtScannerComponent", cg.Component
)
BTHomeReading = ble_ext_scanner_ns.struct("BTHomeReading")
BLEExtScannerTrigger = ble_ext_scanner_ns.class_(
    "BLEExtScannerTrigger", automation.Trigger.template(BTHomeReading)
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(BLEExtScannerComponent),
        cv.Optional(CONF_SCAN_INTERVAL, default=1100): cv.int_range(min=10, max=10240),
        cv.Optional(CONF_SCAN_WINDOW, default=1100): cv.int_range(min=10, max=10240),
        cv.Optional(CONF_SCAN_CODED, default=True): cv.boolean,
        cv.Optional(CONF_SCAN_1M, default=True): cv.boolean,
        cv.Optional(CONF_ON_READING): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(BLEExtScannerTrigger),
            }
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    # Enable Bluetooth in ESP-IDF build
    add_idf_sdkconfig_option("CONFIG_BT_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_BT_BLUEDROID_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_BT_BLE_50_FEATURES_SUPPORTED", True)

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_scan_interval(config[CONF_SCAN_INTERVAL]))
    cg.add(var.set_scan_window(config[CONF_SCAN_WINDOW]))
    cg.add(var.set_scan_coded(config[CONF_SCAN_CODED]))
    cg.add(var.set_scan_1m(config[CONF_SCAN_1M]))

    for conf in config.get(CONF_ON_READING, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(BTHomeReading, "x")], conf)
