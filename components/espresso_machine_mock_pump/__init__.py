"""
espresso_machine_mock_pump — Pump + flow simulation for PID testing.

This component provides a fully simulated pump that replaces both the physical
pump relay and flow meter. The orchestrator sees no difference — it controls
the pump via IPump interface and reads flow data from the same interface.

Pump Curve + Pressure-Weighted Wetting Model:
    Q_ss  = Q_max × (1 − P_puck / P_stall)           [linear pump curve]
    Q_max = nominal_flow / (1 − 9 / pump_max_pressure_bar)  [calibrated at 9 bar]
    Q(t)  = Q_ss × (1 − exp(−t / effective_τ))

    Wetting time scales with puck resistance:
    effective_τ = puck_time_constant × (P_puck / 9 bar)

    nominal_flow          = flow at 9 bar rated pressure [mL/s]
    pump_max_pressure_bar = pump stall pressure [bar]  (Ulka EP5 ≈ 15 bar)
    puck_pressure_bar     = puck back-pressure resistance [bar]
    puck_time_constant_s  = wetting time constant at 9 bar reference [s]

Flow effects of different puck resistances (P_stall = 15 bar, Q_nom = 4 mL/s):
    6 bar  (easy puck)  → Q_ss = 6.0 mL/s, wetting τ_eff = 6.7 s (shorter)
    9 bar  (nominal)    → Q_ss = 4.0 mL/s, wetting τ_eff = 10 s
    12 bar (hard puck)  → Q_ss = 2.0 mL/s, wetting τ_eff = 13.3 s (longer)
    15 bar (stall)      → Q_ss = 0          (pump stalls, only wetting)

Residual Pressure Decay Model (internal_volume_ml):
    When the pump stops, the pressurised water trapped inside the machine's
    tubing and piping continues to drive flow through the puck/valve path
    until the pressure bleeds off.  This produces a realistic gradual flow
    decay rather than an instant drop to zero.

    system_pressure(t) = P_stop × exp(−t / τ_decay)
    Q_residual(t)      = Q_ss × (system_pressure(t) / P_puck)
    τ_decay            = internal_volume_ml / nominal_flow   [s]

    Example with internal_volume_ml = 20 mL, nominal_flow = 4 mL/s:
        τ_decay = 5 s  → flow is at ~37 % of Q_ss after 5 s
        flow is effectively zero after ~25 s (5 × τ)

    Set internal_volume_ml = 0 to disable the model and revert to legacy
    instant-decay behaviour.

All physics parameters are exposed as HA number entities — adjustable without
reflashing.

Example usage:

    espresso_machine_mock_pump:
      id: main_pump
      name: "Mock Pump"
      nominal_flow_ml_per_s: 4.0
      pump_max_pressure_bar: 15.0
      puck_time_constant_s: 10.0
      puck_pressure_bar: 9.0
      internal_volume_ml: 20.0
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
CONF_PUMP_MAX_PRESSURE_BAR = "pump_max_pressure_bar"
CONF_PUCK_TIME_CONSTANT_S = "puck_time_constant_s"
CONF_PUCK_PRESSURE_BAR = "puck_pressure_bar"
CONF_INTERNAL_VOLUME_ML = "internal_volume_ml"
CONF_MOCK_HEATER = "mock_heater"
CONF_RATE_SENSOR = "rate_sensor"
CONF_TOTAL_SENSOR = "total_sensor"

# Number entity config keys for runtime tuning
CONF_NOMINAL_FLOW_NUMBER = "nominal_flow_number"
CONF_PUMP_MAX_PRESSURE_NUMBER = "pump_max_pressure_number"
CONF_PUCK_TIME_CONSTANT_NUMBER = "puck_time_constant_number"
CONF_PUCK_PRESSURE_NUMBER = "puck_pressure_number"
CONF_INTERNAL_VOLUME_NUMBER = "internal_volume_number"

CONFIG_SCHEMA = (
    switch.switch_schema(MockPump)
    .extend(
        {
            # Pump hardware: flow at 9 bar rated pressure
            cv.Optional(CONF_NOMINAL_FLOW_ML_PER_S, default=4.0): cv.positive_float,
            # Pump stall pressure (Ulka EP5 ≈ 15 bar). All flow ceases above this.
            # Must be strictly greater than the 9 bar rated pressure.
            cv.Optional(CONF_PUMP_MAX_PRESSURE_BAR, default=15.0): cv.All(
                cv.positive_float, cv.Range(min=9.01)
            ),
            # Puck wetting time constant at 9 bar (scales proportionally with pressure)
            cv.Optional(CONF_PUCK_TIME_CONSTANT_S, default=10.0): cv.positive_float,
            # Puck back-pressure. Affects both steady-state flow and wetting duration.
            cv.Optional(CONF_PUCK_PRESSURE_BAR, default=9.0): cv.positive_float,
            # Internal volume of tubing and piping [mL].
            # Governs how long residual pressure drives flow after the pump stops.
            # τ_decay = internal_volume_ml / nominal_flow_ml_per_s  (e.g. 20mL/4mL·s⁻¹ = 5s)
            # Set to 0 to disable the model and use the legacy instant-decay behaviour.
            cv.Optional(CONF_INTERNAL_VOLUME_ML, default=20.0): cv.positive_float,
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
            cv.Optional(CONF_PUMP_MAX_PRESSURE_NUMBER): number.number_schema(
                MockPumpNumber
            ).extend(cv.COMPONENT_SCHEMA),
            cv.Optional(CONF_PUCK_TIME_CONSTANT_NUMBER): number.number_schema(
                MockPumpNumber
            ).extend(cv.COMPONENT_SCHEMA),
            cv.Optional(CONF_PUCK_PRESSURE_NUMBER): number.number_schema(
                MockPumpNumber
            ).extend(cv.COMPONENT_SCHEMA),
            cv.Optional(CONF_INTERNAL_VOLUME_NUMBER): number.number_schema(
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
    cg.add(var.set_pump_max_pressure(config[CONF_PUMP_MAX_PRESSURE_BAR]))
    cg.add(var.set_puck_time_constant(config[CONF_PUCK_TIME_CONSTANT_S]))
    cg.add(var.set_puck_pressure(config[CONF_PUCK_PRESSURE_BAR]))
    cg.add(var.set_internal_volume(config[CONF_INTERNAL_VOLUME_ML]))

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

    if CONF_PUMP_MAX_PRESSURE_NUMBER in config:
        num_conf = config[CONF_PUMP_MAX_PRESSURE_NUMBER]
        num_var = await number.new_number(
            num_conf, min_value=10.0, max_value=20.0, step=0.5
        )
        await cg.register_component(num_var, num_conf)
        cg.add(var.set_pump_max_pressure_number(num_var))
        cg.add(num_var.set_parent(var))

    if CONF_PUCK_TIME_CONSTANT_NUMBER in config:
        num_conf = config[CONF_PUCK_TIME_CONSTANT_NUMBER]
        num_var = await number.new_number(
            num_conf, min_value=0.1, max_value=60.0, step=0.5
        )
        await cg.register_component(num_var, num_conf)
        cg.add(var.set_puck_time_constant_number(num_var))
        cg.add(num_var.set_parent(var))

    if CONF_PUCK_PRESSURE_NUMBER in config:
        num_conf = config[CONF_PUCK_PRESSURE_NUMBER]
        num_var = await number.new_number(
            num_conf, min_value=0.0, max_value=16.0, step=0.5
        )
        await cg.register_component(num_var, num_conf)
        cg.add(var.set_puck_pressure_number(num_var))
        cg.add(num_var.set_parent(var))

    if CONF_INTERNAL_VOLUME_NUMBER in config:
        num_conf = config[CONF_INTERNAL_VOLUME_NUMBER]
        num_var = await number.new_number(
            num_conf, min_value=0.0, max_value=200.0, step=1.0
        )
        await cg.register_component(num_var, num_conf)
        cg.add(var.set_internal_volume_number(num_var))
        cg.add(num_var.set_parent(var))
