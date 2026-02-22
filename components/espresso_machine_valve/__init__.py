import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.const import CONF_ID, CONF_NAME, CONF_PIN

CODEOWNERS = ["@shaggitza"]
MULTI_CONF = True

espresso_machine_valve_ns = cg.esphome_ns.namespace("espresso_machine_valve")
Valve = espresso_machine_valve_ns.class_("Valve", cg.Component)

CONF_NORMALLY_OPEN = "normally_open"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Valve),
        cv.Optional(CONF_NAME): cv.string,
        cv.Required(CONF_PIN): pins.gpio_output_pin_schema,
        cv.Optional(CONF_NORMALLY_OPEN, default=False): cv.boolean,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    pin = await cg.gpio_pin_expression(config[CONF_PIN])
    cg.add(var.set_pin(pin))
    cg.add(var.set_normally_open(config[CONF_NORMALLY_OPEN]))
