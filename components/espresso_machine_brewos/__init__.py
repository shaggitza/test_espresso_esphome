import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@shaggitza"]

# No DEPENDENCIES — wifi is optional; the connector handles cloud-unreachable gracefully.

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

    # SAFETY: Reduce WebSocket TCP timeout from default 5000ms to 500ms.
    # This limits how long the control loop can be blocked during reconnection.
    cg.add_build_flag("-DWEBSOCKETS_TCP_TIMEOUT=500")
