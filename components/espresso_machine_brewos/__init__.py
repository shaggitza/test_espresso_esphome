import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@shaggitza"]

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
    # Note: the WebSocket client (links2004/WebSockets) and Preferences headers
    # are only compiled under #ifdef ARDUINO.  Add the library to your
    # platformio_options when targeting ESP32:
    #
    #   esphome:
    #     platformio_options:
    #       lib_deps:
    #         - links2004/WebSockets@^2.4.0
