import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation, pins
from esphome.components import switch, number
from esphome.const import (
    CONF_ID,
    CONF_PIN,
    CONF_TYPE,
    UNIT_PERCENT,
)

CODEOWNERS = ["@shaggitza"]
MULTI_CONF = True
AUTO_LOAD = ["switch", "number"]

espresso_machine_pump_ns = cg.esphome_ns.namespace("espresso_machine_pump")
PumpSwitch = espresso_machine_pump_ns.class_(
    "PumpSwitch", switch.Switch, cg.Component
)
PumpNumber = espresso_machine_pump_ns.class_(
    "PumpNumber", number.Number, cg.Component
)
RunAction = espresso_machine_pump_ns.class_("RunAction", automation.Action)

CONF_FLOW_METER = "flow_meter"
CONF_VOLUME_ML = "volume_ml"
CONF_TIMEOUT_MS = "timeout_ms"

RELAY_SCHEMA = (
    switch.switch_schema(PumpSwitch)
    .extend(
        {
            cv.Required(CONF_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_FLOW_METER): cv.use_id(cg.Component),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)

DIMMER_SCHEMA = (
    number.number_schema(
        PumpNumber,
        unit_of_measurement=UNIT_PERCENT,
    )
    .extend(
        {
            cv.Required(CONF_PIN): pins.gpio_output_pin_schema,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)

CONFIG_SCHEMA = cv.typed_schema(
    {
        "relay": RELAY_SCHEMA,
        "dimmer": DIMMER_SCHEMA,
    },
    key=CONF_TYPE,
)

RUN_ACTION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.use_id(PumpSwitch),
        cv.Required(CONF_VOLUME_ML): cv.templatable(cv.positive_float),
        cv.Optional(CONF_TIMEOUT_MS, default="0s"): cv.templatable(
            cv.positive_time_period_milliseconds
        ),
    }
)


@automation.register_action(
    "espresso_machine_pump.run", RunAction, RUN_ACTION_SCHEMA
)
async def pump_run_action_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    template_vol = await cg.templatable(config[CONF_VOLUME_ML], args, cg.float_)
    cg.add(var.set_volume_ml(template_vol))
    template_timeout = await cg.templatable(config[CONF_TIMEOUT_MS], args, cg.uint32)
    cg.add(var.set_timeout_ms(template_timeout))
    return var


async def to_code(config):
    pump_type = config[CONF_TYPE]

    if pump_type == "relay":
        var = await switch.new_switch(config)
        await cg.register_component(var, config)
        if CONF_FLOW_METER in config:
            flow_meter = await cg.get_variable(config[CONF_FLOW_METER])
            cg.add(var.set_flow_meter(flow_meter))
    else:
        var = await number.new_number(
            config, min_value=0.0, max_value=100.0, step=1.0
        )
        await cg.register_component(var, config)

    pin = await cg.gpio_pin_expression(config[CONF_PIN])
    cg.add(var.set_pin(pin))
