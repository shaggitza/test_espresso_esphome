import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation, pins
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    CONF_PIN,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
)

CODEOWNERS = ["@shaggitza"]
MULTI_CONF = True
AUTO_LOAD = ["sensor"]

espresso_machine_flow_meter_ns = cg.esphome_ns.namespace(
    "espresso_machine_flow_meter"
)
FlowMeter = espresso_machine_flow_meter_ns.class_("FlowMeter", cg.Component)
ResetAction = espresso_machine_flow_meter_ns.class_("ResetAction", automation.Action)
CalibrateAction = espresso_machine_flow_meter_ns.class_(
    "CalibrateAction", automation.Action
)

CONF_PULSES_PER_ML = "pulses_per_ml"
CONF_RATE_SENSOR = "rate_sensor"
CONF_TOTAL_SENSOR = "total_sensor"
CONF_AVG_RATE_SENSOR = "avg_rate_sensor"
CONF_ACTUAL_VOLUME_ML = "actual_volume_ml"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(FlowMeter),
        cv.Optional(CONF_NAME): cv.string,
        cv.Required(CONF_PIN): pins.internal_gpio_input_pin_schema,
        cv.Required(CONF_PULSES_PER_ML): cv.positive_float,
        cv.Optional(CONF_RATE_SENSOR): sensor.sensor_schema(
            unit_of_measurement="mL/s",
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_TOTAL_SENSOR): sensor.sensor_schema(
            unit_of_measurement="mL",
            accuracy_decimals=1,
            state_class=STATE_CLASS_TOTAL_INCREASING,
        ),
        cv.Optional(CONF_AVG_RATE_SENSOR): sensor.sensor_schema(
            unit_of_measurement="mL/s",
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    pin = await cg.gpio_pin_expression(config[CONF_PIN])
    cg.add(var.set_pin(pin))
    cg.add(var.set_pulses_per_ml(config[CONF_PULSES_PER_ML]))

    if CONF_RATE_SENSOR in config:
        sens = await sensor.new_sensor(config[CONF_RATE_SENSOR])
        cg.add(var.set_rate_sensor(sens))

    if CONF_TOTAL_SENSOR in config:
        sens = await sensor.new_sensor(config[CONF_TOTAL_SENSOR])
        cg.add(var.set_total_sensor(sens))

    if CONF_AVG_RATE_SENSOR in config:
        sens = await sensor.new_sensor(config[CONF_AVG_RATE_SENSOR])
        cg.add(var.set_avg_rate_sensor(sens))


# ---------------------------------------------------------------------------
# Automation actions
# ---------------------------------------------------------------------------

RESET_ACTION_SCHEMA = automation.maybe_simple_id(
    {cv.GenerateID(CONF_ID): cv.use_id(FlowMeter)}
)


@automation.register_action(
    "espresso_machine_flow_meter.reset", ResetAction, RESET_ACTION_SCHEMA
)
async def reset_action_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


CALIBRATE_ACTION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.use_id(FlowMeter),
        cv.Required(CONF_ACTUAL_VOLUME_ML): cv.templatable(cv.positive_float),
    }
)


@automation.register_action(
    "espresso_machine_flow_meter.calibrate", CalibrateAction, CALIBRATE_ACTION_SCHEMA
)
async def calibrate_action_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    template_ = await cg.templatable(
        config[CONF_ACTUAL_VOLUME_ML], args, cg.float_
    )
    cg.add(var.set_actual_volume_ml(template_))
    return var
