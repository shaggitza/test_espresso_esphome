import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.const import CONF_ID, CONF_NAME, CONF_PIN, CONF_TYPE

CODEOWNERS = ["@shaggitza"]
MULTI_CONF = True

espresso_machine_grinder_ns = cg.esphome_ns.namespace("espresso_machine_grinder")
Grinder = espresso_machine_grinder_ns.class_("Grinder", cg.Component)
GrinderType = espresso_machine_grinder_ns.enum("GrinderType")

GRINDER_TYPES = {
    "relay": GrinderType.RELAY,
    "none": GrinderType.NONE,
}

CONF_DEFAULT_GRIND_TIME = "default_grind_time"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Grinder),
        cv.Optional(CONF_NAME): cv.string,
        cv.Required(CONF_TYPE): cv.enum(GRINDER_TYPES, lower=True),
        cv.Optional(CONF_PIN): pins.gpio_output_pin_schema,
        cv.Optional(CONF_DEFAULT_GRIND_TIME, default="7s"): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_grinder_type(config[CONF_TYPE]))
    cg.add(var.set_default_grind_time(config[CONF_DEFAULT_GRIND_TIME].total_milliseconds))

    if CONF_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_PIN])
        cg.add(var.set_pin(pin))
