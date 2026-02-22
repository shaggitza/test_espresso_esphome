import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation, pins
from esphome.components import switch
from esphome.const import CONF_ID, CONF_PIN

CODEOWNERS = ["@shaggitza"]
MULTI_CONF = True

espresso_machine_valve_ns = cg.esphome_ns.namespace("espresso_machine_valve")
Valve = espresso_machine_valve_ns.class_("Valve", switch.Switch, cg.Component)
OpenAction = espresso_machine_valve_ns.class_("OpenAction", automation.Action)
CloseAction = espresso_machine_valve_ns.class_("CloseAction", automation.Action)

CONF_NORMALLY_OPEN = "normally_open"

CONFIG_SCHEMA = (
    switch.switch_schema(Valve)
    .extend(
        {
            cv.Required(CONF_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_NORMALLY_OPEN, default=False): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)

OPEN_ACTION_SCHEMA = automation.maybe_simple_id(
    {cv.GenerateID(CONF_ID): cv.use_id(Valve)}
)

CLOSE_ACTION_SCHEMA = automation.maybe_simple_id(
    {cv.GenerateID(CONF_ID): cv.use_id(Valve)}
)


@automation.register_action(
    "espresso_machine_valve.open", OpenAction, OPEN_ACTION_SCHEMA
)
async def valve_open_action(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


@automation.register_action(
    "espresso_machine_valve.close", CloseAction, CLOSE_ACTION_SCHEMA
)
async def valve_close_action(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


async def to_code(config):
    var = await switch.new_switch(config)
    await cg.register_component(var, config)

    pin = await cg.gpio_pin_expression(config[CONF_PIN])
    cg.add(var.set_pin(pin))
    cg.add(var.set_normally_open(config[CONF_NORMALLY_OPEN]))
