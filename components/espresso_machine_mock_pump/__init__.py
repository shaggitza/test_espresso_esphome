"""
espresso_machine_mock_pump — Pump + flow simulation for PID testing.

This component provides a fully simulated pump that replaces both the physical
pump relay and flow meter. The orchestrator sees no difference — it controls
the pump via IPump interface and reads flow data from the same interface.

Puck Wetting Model:
    Q(t) = Q_nom × (1 − exp(−t/τ))

    Q_nom = nominal flow rate [mL/s]
    τ     = puck time constant [s] (how quickly flow ramps after pump start)
    t     = time since pump started [s]

This models the physical behavior where flow starts near zero (dry puck
compresses under pressure) and ramps exponentially to the nominal rate as
channels form through the coffee bed.

All physics parameters are exposed as HA number entities — adjustable without
reflashing.

Example usage:

    espresso_machine_mock_pump:
      id: main_pump
      name: "Mock Pump"
      nominal_flow_ml_per_s: 4.0
      puck_time_constant_s: 10.0
      rate_sensor:
        name: "Brew Flow Rate"
      total_sensor:
        name: "Brew Flow Total"
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import switch, sensor, number
from esphome.const import (
    CONF_ID,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
)

CODEOWNERS = ["@shaggitza"]
AUTO_LOAD = ["switch", "sensor", "number"]

espresso_machine_mock_pump_ns = cg.esphome_ns.namespace("espresso_machine_mock_pump")

MockPump = espresso_machine_mock_pump_ns.class_(
    "MockPump", switch.Switch, cg.Component
)
MockPumpNumber = espresso_machine_mock_pump_ns.class_(
    "MockPumpNumber", number.Number, cg.Component
)
ResetAction = espresso_machine_mock_pump_ns.class_("ResetAction", automation.Action)

# Config keys
CONF_NOMINAL_FLOW_ML_PER_S = "nominal_flow_ml_per_s"
CONF_PUCK_TIME_CONSTANT_S = "puck_time_constant_s"
CONF_MOCK_HEATER = "mock_heater"
CONF_RATE_SENSOR = "rate_sensor"
CONF_TOTAL_SENSOR = "total_sensor"

# Number entity config keys for runtime tuning
CONF_NOMINAL_FLOW_NUMBER = "nominal_flow_number"
CONF_PUCK_TIME_CONSTANT_NUMBER = "puck_time_constant_number"

CONFIG_SCHEMA = (
    switch.switch_schema(MockPump)
    .extend(
        {
            # Physics parameters (defaults model a typical espresso extraction)
            cv.Optional(CONF_NOMINAL_FLOW_ML_PER_S, default=4.0): cv.positive_float,
            cv.Optional(CONF_PUCK_TIME_CONSTANT_S, default=10.0): cv.positive_float,
            # Optional link to mock heater — drives flow-based thermoblock cooling
            cv.Optional(CONF_MOCK_HEATER): cv.use_id(cg.Component),
            # Flow sensor sub-entities (same interface as espresso_machine_flow_meter)
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
            # Optional HA number entities for runtime tuning
            cv.Optional(CONF_NOMINAL_FLOW_NUMBER): number.number_schema(
                MockPumpNumber
            ).extend(cv.COMPONENT_SCHEMA),
            cv.Optional(CONF_PUCK_TIME_CONSTANT_NUMBER): number.number_schema(
                MockPumpNumber
            ).extend(cv.COMPONENT_SCHEMA),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)

# Reset action (clears accumulated volume)
RESET_ACTION_SCHEMA = automation.maybe_simple_id(
    {cv.GenerateID(CONF_ID): cv.use_id(MockPump)}
)


@automation.register_action(
    "espresso_machine_mock_pump.reset", ResetAction, RESET_ACTION_SCHEMA
)
async def reset_action_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


async def to_code(config):
    var = await switch.new_switch(config)
    await cg.register_component(var, config)

    # Set physics parameters
    cg.add(var.set_nominal_flow(config[CONF_NOMINAL_FLOW_ML_PER_S]))
    cg.add(var.set_puck_time_constant(config[CONF_PUCK_TIME_CONSTANT_S]))

    # Wire to mock heater for flow-based thermoblock cooling
    if CONF_MOCK_HEATER in config:
        heater = await cg.get_variable(config[CONF_MOCK_HEATER])
        cg.add(var.set_heater(heater))

    # Create and register flow rate sensor
    if CONF_RATE_SENSOR in config:
        sens = await sensor.new_sensor(config[CONF_RATE_SENSOR])
        cg.add(var.set_rate_sensor(sens))

    # Create and register flow total sensor
    if CONF_TOTAL_SENSOR in config:
        sens = await sensor.new_sensor(config[CONF_TOTAL_SENSOR])
        cg.add(var.set_total_sensor(sens))

    # Optional runtime-tunable number entities
    if CONF_NOMINAL_FLOW_NUMBER in config:
        num_conf = config[CONF_NOMINAL_FLOW_NUMBER]
        num_var = await number.new_number(
            num_conf, min_value=0.0, max_value=20.0, step=0.1
        )
        await cg.register_component(num_var, num_conf)
        cg.add(var.set_nominal_flow_number(num_var))
        cg.add(num_var.set_parent(var))

    if CONF_PUCK_TIME_CONSTANT_NUMBER in config:
        num_conf = config[CONF_PUCK_TIME_CONSTANT_NUMBER]
        num_var = await number.new_number(
            num_conf, min_value=0.1, max_value=60.0, step=0.5
        )
        await cg.register_component(num_var, num_conf)
        cg.add(var.set_puck_time_constant_number(num_var))
        cg.add(num_var.set_parent(var))
