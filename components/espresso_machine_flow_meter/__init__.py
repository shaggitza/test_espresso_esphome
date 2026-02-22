import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.const import CONF_ID, CONF_NAME, CONF_PIN

CODEOWNERS = ["@shaggitza"]
MULTI_CONF = True

espresso_machine_flow_meter_ns = cg.esphome_ns.namespace(
    "espresso_machine_flow_meter"
)
FlowMeter = espresso_machine_flow_meter_ns.class_("FlowMeter", cg.Component)

CONF_PULSES_PER_ML = "pulses_per_ml"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(FlowMeter),
        cv.Optional(CONF_NAME): cv.string,
        cv.Required(CONF_PIN): pins.gpio_input_pin_schema,
        cv.Required(CONF_PULSES_PER_ML): cv.positive_float,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    pin = await cg.gpio_pin_expression(config[CONF_PIN])
    cg.add(var.set_pin(pin))
    cg.add(var.set_pulses_per_ml(config[CONF_PULSES_PER_ML]))
