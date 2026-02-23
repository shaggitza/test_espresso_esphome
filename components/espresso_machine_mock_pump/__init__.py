"""
espresso_machine_mock_pump — Pump + flow simulation for PID testing.

This component provides a fully simulated pump that replaces both the physical
pump relay and flow meter. The orchestrator sees no difference — it controls
the pump via IPump interface and reads flow data from the same interface.

Puck Density Model:
    puck_density   = 1..100 (dimensionless scale)
    flow_fraction  = (101 − D) / 100         →  1.0 at D=1,  0.01 at D=100
    Q_ss           = nominal_flow × flow_fraction
    P_equilibrium  = pump_max_pressure × (D − 1) / 100
                     →  0 bar at D=1 (open puck),  ~P_max at D=100 (blocked)

    Examples with nominal_flow = 4 mL/s, pump_max_pressure = 15 bar:
        D=1   (open puck)   → Q = 4.0 mL/s, P_eq = 0.0 bar
        D=25  (soft puck)   → Q = 3.0 mL/s, P_eq = 3.6 bar
        D=50  (medium puck) → Q = 2.0 mL/s, P_eq = 7.35 bar
        D=75  (hard puck)   → Q = 1.04 mL/s, P_eq = 11.1 bar
        D=100 (blocked)     → Q = 0.04 mL/s, P_eq = 14.85 bar

Pressure Model:
    When the pump starts, system pressure rises quickly toward P_equilibrium
    using a first-order response with τ_rise = 1.5 s (~95% reached in 4.5 s).
    This replaces the old slow wetting-based pressure buildup with physically
    realistic fast pressure build-up followed by stabilisation at puck resistance.

Wetting Model:
    effective_τ = puck_time_constant × (D / 100)
    wetted_fraction(t) = 1 − exp(−t / effective_τ)
    Q(t) = Q_ss × wetted_fraction(t)

Residual Pressure Decay Model (internal_volume_ml):
    When the pump stops, the pressurised water trapped inside the machine's
    tubing and piping continues to drive flow through the puck/valve path
    until the pressure bleeds off.  This produces a realistic gradual flow
    decay rather than an instant drop to zero.

    system_pressure(t) = P_stop × exp(−t / τ_decay)
    Q_residual(t)      = Q_ss × (system_pressure(t) / P_equilibrium)
    τ_decay            = internal_volume_ml / nominal_flow   [s]

    Set internal_volume_ml = 0 to disable the model and revert to legacy
    instant-decay behaviour.

Nozzle Flow Model:
    The nozzle flow represents the water that exits the coffee puck into the cup,
    which differs from the pump flow (water entering the puck) due to puck absorption.
    
    Coffee grounds absorb water during extraction (~2ml per gram of coffee).
    A typical 18g dose absorbs ~36ml total, primarily during the wetting phase.
    
    The absorption follows the same wetting curve as flow breakthrough:
        absorbed_total(t) = puck_absorption_ml × wetted_fraction(t)
        absorption_rate   = d(absorbed_total)/dt
                          = puck_absorption_ml × (1/τ_eff) × exp(-t/τ_eff)
    
    Nozzle flow = pump flow - absorption rate
    
    This means early in the shot, much of the water is absorbed by the puck
    and little comes out the nozzle. As the puck saturates (approaches full
    wetted_fraction), absorption rate drops toward zero and nozzle flow
    approaches pump flow.

Nozzle Flow Sensors:
    nozzle_rate_sensor and nozzle_total_sensor track the estimated flow out
    of the group head nozzle after accounting for puck absorption.
    These represent the actual espresso yield in the cup.

All physics parameters are exposed as HA number entities — adjustable without
reflashing.  Use entity_category: config in the number sub-schemas to keep
tuning controls separate from primary sensors in the HA device page.

Example usage:

    espresso_machine_mock_pump:
      id: main_pump
      name: "Mock Pump"
      nominal_flow_ml_per_s: 4.0
      pump_max_pressure_bar: 15.0
      puck_time_constant_s: 10.0
      puck_density: 50
      internal_volume_ml: 20.0
      puck_absorption_ml: 36.0
      rate_sensor:
        name: "Brew Flow Rate"
      total_sensor:
        name: "Brew Flow Total"
      nozzle_rate_sensor:
        name: "Nozzle Flow Rate"
      nozzle_total_sensor:
        name: "Nozzle Flow Total"
      pressure_sensor:
        name: "Brew Pump Pressure"
      puck_density_number:
        name: "Mock Puck Density"
        entity_category: config
      puck_absorption_number:
        name: "Mock Puck Absorption"
        entity_category: config
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
CONF_PUCK_DENSITY = "puck_density"
CONF_INTERNAL_VOLUME_ML = "internal_volume_ml"
CONF_PUCK_ABSORPTION_ML = "puck_absorption_ml"
CONF_MOCK_HEATER = "mock_heater"
CONF_RATE_SENSOR = "rate_sensor"
CONF_TOTAL_SENSOR = "total_sensor"
CONF_PRESSURE_SENSOR = "pressure_sensor"
CONF_NOZZLE_RATE_SENSOR = "nozzle_rate_sensor"
CONF_NOZZLE_TOTAL_SENSOR = "nozzle_total_sensor"

# Number entity config keys for runtime tuning
CONF_NOMINAL_FLOW_NUMBER = "nominal_flow_number"
CONF_PUMP_MAX_PRESSURE_NUMBER = "pump_max_pressure_number"
CONF_PUCK_TIME_CONSTANT_NUMBER = "puck_time_constant_number"
CONF_PUCK_DENSITY_NUMBER = "puck_density_number"
CONF_INTERNAL_VOLUME_NUMBER = "internal_volume_number"
CONF_PUCK_ABSORPTION_NUMBER = "puck_absorption_number"

CONFIG_SCHEMA = (
    switch.switch_schema(MockPump)
    .extend(
        {
            # Max unimpeded flow with no puck resistance (D=1) [mL/s]
            cv.Optional(CONF_NOMINAL_FLOW_ML_PER_S, default=4.0): cv.positive_float,
            # Pump stall pressure (Ulka EP5 ≈ 15 bar). All flow ceases above this.
            # Must be strictly greater than 9 bar.
            cv.Optional(CONF_PUMP_MAX_PRESSURE_BAR, default=15.0): cv.All(
                cv.positive_float, cv.Range(min=9.01)
            ),
            # Puck wetting time constant at D=100 (scales down with density)
            cv.Optional(CONF_PUCK_TIME_CONSTANT_S, default=10.0): cv.positive_float,
            # Puck density: 1=fully open (max flow), 100=fully blocked (~0 flow).
            # Drives both steady-state flow (Q_ss) and equilibrium pressure (P_eq).
            cv.Optional(CONF_PUCK_DENSITY, default=50.0): cv.All(
                cv.positive_float, cv.Range(min=1.0, max=100.0)
            ),
            # Internal volume of tubing and piping [mL].
            # Governs how long residual pressure drives flow after the pump stops.
            # τ_decay = internal_volume_ml / nominal_flow_ml_per_s  (e.g. 20mL/4mL·s⁻¹ = 5s)
            # Set to 0 to disable the model and use the legacy instant-decay behaviour.
            cv.Optional(CONF_INTERNAL_VOLUME_ML, default=20.0): cv.positive_float,
            # Puck water absorption capacity [mL].
            # Coffee grounds absorb water during extraction (~2ml per gram of coffee).
            # A typical 18g dose absorbs ~36ml. This absorption happens primarily during
            # the wetting phase and reduces nozzle output compared to pump input.
            cv.Optional(CONF_PUCK_ABSORPTION_ML, default=36.0): cv.positive_float,
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
            cv.Optional(CONF_PRESSURE_SENSOR): sensor.sensor_schema(
                unit_of_measurement="bar",
                accuracy_decimals=2,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            # Nozzle flow sensors — estimated output from the group head nozzle
            cv.Optional(CONF_NOZZLE_RATE_SENSOR): sensor.sensor_schema(
                unit_of_measurement="mL/s",
                accuracy_decimals=1,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_NOZZLE_TOTAL_SENSOR): sensor.sensor_schema(
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
            cv.Optional(CONF_PUCK_DENSITY_NUMBER): number.number_schema(
                MockPumpNumber
            ).extend(cv.COMPONENT_SCHEMA),
            cv.Optional(CONF_INTERNAL_VOLUME_NUMBER): number.number_schema(
                MockPumpNumber
            ).extend(cv.COMPONENT_SCHEMA),
            cv.Optional(CONF_PUCK_ABSORPTION_NUMBER): number.number_schema(
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
    cg.add(var.set_puck_density(config[CONF_PUCK_DENSITY]))
    cg.add(var.set_internal_volume(config[CONF_INTERNAL_VOLUME_ML]))
    cg.add(var.set_puck_absorption(config[CONF_PUCK_ABSORPTION_ML]))

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

    # Create and register pressure sensor
    if CONF_PRESSURE_SENSOR in config:
        sens = await sensor.new_sensor(config[CONF_PRESSURE_SENSOR])
        cg.add(var.set_pressure_sensor(sens))

    # Create and register nozzle flow sensors
    if CONF_NOZZLE_RATE_SENSOR in config:
        sens = await sensor.new_sensor(config[CONF_NOZZLE_RATE_SENSOR])
        cg.add(var.set_nozzle_rate_sensor(sens))

    if CONF_NOZZLE_TOTAL_SENSOR in config:
        sens = await sensor.new_sensor(config[CONF_NOZZLE_TOTAL_SENSOR])
        cg.add(var.set_nozzle_total_sensor(sens))

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

    if CONF_PUCK_DENSITY_NUMBER in config:
        num_conf = config[CONF_PUCK_DENSITY_NUMBER]
        num_var = await number.new_number(
            num_conf, min_value=1.0, max_value=100.0, step=1.0
        )
        await cg.register_component(num_var, num_conf)
        cg.add(var.set_puck_density_number(num_var))
        cg.add(num_var.set_parent(var))

    if CONF_INTERNAL_VOLUME_NUMBER in config:
        num_conf = config[CONF_INTERNAL_VOLUME_NUMBER]
        num_var = await number.new_number(
            num_conf, min_value=0.0, max_value=200.0, step=1.0
        )
        await cg.register_component(num_var, num_conf)
        cg.add(var.set_internal_volume_number(num_var))
        cg.add(num_var.set_parent(var))

    if CONF_PUCK_ABSORPTION_NUMBER in config:
        num_conf = config[CONF_PUCK_ABSORPTION_NUMBER]
        num_var = await number.new_number(
            num_conf, min_value=0.0, max_value=100.0, step=1.0
        )
        await cg.register_component(num_var, num_conf)
        cg.add(var.set_puck_absorption_number(num_var))
        cg.add(num_var.set_parent(var))
