import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@shaggitza"]

# This component requires WiFi to be configured (for WebSocket connectivity).
DEPENDENCIES = ["wifi"]

# Reference the EspressoMachine class by namespace (avoids Python circular import).
espresso_machine_ns = cg.esphome_ns.namespace("espresso_machine")
EspressoMachine = espresso_machine_ns.class_("EspressoMachine", cg.Component)

espresso_machine_brewos_ns = cg.esphome_ns.namespace("espresso_machine_brewos")
BrewOSConnector = espresso_machine_brewos_ns.class_("BrewOSConnector", cg.Component)

CONF_URL = "url"
CONF_ESPRESSO_MACHINE = "espresso_machine"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(BrewOSConnector),
        # BrewOS cloud base URL, e.g. "https://cloud.brewos.io"
        cv.Required(CONF_URL): cv.string,
        # ID of the espresso_machine: orchestrator to monitor and control.
        cv.Required(CONF_ESPRESSO_MACHINE): cv.use_id(EspressoMachine),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_url(config[CONF_URL]))

    machine = await cg.get_variable(config[CONF_ESPRESSO_MACHINE])
    cg.add(var.set_espresso_machine(machine))

    # WebSocket client library (only used in Arduino builds; guarded by #ifdef).
    # Use the library name format that works with ESPHome's lib_ldf_mode=off.
    cg.add_library("links2004/WebSockets", "^2.4.0")
    # Arduino ESP32 framework libraries required by WebSockets — with lib_ldf_mode=off
    # these must be added explicitly since PlatformIO won't auto-discover them.
    cg.add_library("WiFi", None)
    cg.add_library("NetworkClientSecure", None)  # WiFiClientSecure.h (ESP32 3.x)
