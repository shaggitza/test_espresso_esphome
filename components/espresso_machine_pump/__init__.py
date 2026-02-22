import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.const import CONF_ID, CONF_NAME, CONF_PIN, CONF_TYPE

CODEOWNERS = ["@shaggitza"]
MULTI_CONF = True

espresso_machine_pump_ns = cg.esphome_ns.namespace("espresso_machine_pump")
Pump = espresso_machine_pump_ns.class_("Pump", cg.Component)
PumpType = espresso_machine_pump_ns.enum("PumpType")

PUMP_TYPES = {
    "relay": PumpType.RELAY,
    "dimmer": PumpType.DIMMER,
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Pump),
        cv.Optional(CONF_NAME): cv.string,
        cv.Required(CONF_TYPE): cv.enum(PUMP_TYPES, lower=True),
        cv.Required(CONF_PIN): pins.gpio_output_pin_schema,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    pin = await cg.gpio_pin_expression(config[CONF_PIN])
    cg.add(var.set_pin(pin))
    cg.add(var.set_pump_type(config[CONF_TYPE]))
