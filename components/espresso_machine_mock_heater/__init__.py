"""
espresso_machine_mock_heater — Thermal simulation for PID testing.

This component provides a fully simulated heater that can replace the physical
thermocouple + slow_pwm output stack. The PID climate entity sees no difference
between the mock and real hardware — it reads the same sensor ID and writes to
the same output ID.

Thermal Model:
    dT/dt = (duty × P − h × (T − T_amb)) / C

    duty   = 0.0–1.0 output from PID (heat_output)
    P      = heater power [W]
    C      = thermal mass [J/°C]  (≈ mass × specific_heat)
    h      = heat-loss coefficient [W/°C]
    T_amb  = ambient temperature [°C]

All physics parameters are exposed as HA number entities — adjustable without
reflashing. The ODE is integrated in loop() every ~10 ms using forward Euler.

Example usage:

    espresso_machine_mock_heater:
      id: mock_heater
      initial_temperature: 25.0
      ambient_temperature: 25.0
      power_watts: 1200.0
      thermal_mass_j_per_c: 1256.0
      heat_loss_w_per_c: 1.7
      temperature_sensor:
        id: thermoblock_temp
        name: "Thermoblock Temperature"
      output:
        id: heater_ssr
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import output, sensor, number
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    CONF_OUTPUT,
    UNIT_CELSIUS,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
)

CODEOWNERS = ["@shaggitza"]
AUTO_LOAD = ["output", "sensor", "number"]

espresso_machine_mock_heater_ns = cg.esphome_ns.namespace(
    "espresso_machine_mock_heater"
)
MockHeater = espresso_machine_mock_heater_ns.class_("MockHeater", cg.Component)
MockHeaterOutput = espresso_machine_mock_heater_ns.class_(
    "MockHeaterOutput", output.FloatOutput, cg.Component
)
MockHeaterTempSensor = espresso_machine_mock_heater_ns.class_(
    "MockHeaterTempSensor", sensor.Sensor, cg.PollingComponent
)
MockHeaterNumber = espresso_machine_mock_heater_ns.class_(
    "MockHeaterNumber", number.Number, cg.Component
)

# Config keys
CONF_TEMPERATURE_SENSOR = "temperature_sensor"
CONF_INITIAL_TEMPERATURE = "initial_temperature"
CONF_AMBIENT_TEMPERATURE = "ambient_temperature"
CONF_POWER_WATTS = "power_watts"
CONF_THERMAL_MASS_J_PER_C = "thermal_mass_j_per_c"
CONF_HEAT_LOSS_W_PER_C = "heat_loss_w_per_c"
CONF_WATER_INLET_TEMP_C = "water_inlet_temp_c"

# Number entity config keys for runtime tuning
CONF_POWER_NUMBER = "power_number"
CONF_THERMAL_MASS_NUMBER = "thermal_mass_number"
CONF_HEAT_LOSS_NUMBER = "heat_loss_number"
CONF_AMBIENT_NUMBER = "ambient_number"

OUTPUT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MockHeaterOutput),
    }
).extend(cv.COMPONENT_SCHEMA)

TEMPERATURE_SENSOR_SCHEMA = sensor.sensor_schema(
    MockHeaterTempSensor,
    unit_of_measurement=UNIT_CELSIUS,
    accuracy_decimals=1,
    device_class=DEVICE_CLASS_TEMPERATURE,
    state_class=STATE_CLASS_MEASUREMENT,
).extend(cv.polling_component_schema("1s"))


def _number_schema(name: str, unit: str, min_val: float, max_val: float, step: float):
    """Helper to generate a number schema for a physics parameter."""
    return number.number_schema(MockHeaterNumber).extend(
        {
            cv.Optional(CONF_NAME, default=name): cv.string,
        }
    ).extend(cv.COMPONENT_SCHEMA)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MockHeater),
        # Initial and ambient temperatures
        cv.Optional(CONF_INITIAL_TEMPERATURE, default=25.0): cv.float_,
        cv.Optional(CONF_AMBIENT_TEMPERATURE, default=25.0): cv.float_,
        # Physics parameters (defaults model a ~10 mL thermoblock at 1.2 kW)
        cv.Optional(CONF_POWER_WATTS, default=1200.0): cv.positive_float,
        cv.Optional(CONF_THERMAL_MASS_J_PER_C, default=42.0): cv.positive_float,
        cv.Optional(CONF_HEAT_LOSS_W_PER_C, default=1.7): cv.positive_float,
        cv.Optional(CONF_WATER_INLET_TEMP_C, default=20.0): cv.float_,
        # Output sub-entity (what the PID's heat_output references)
        cv.Required(CONF_OUTPUT): OUTPUT_SCHEMA,
        # Temperature sensor sub-entity (what the PID's sensor references)
        cv.Required(CONF_TEMPERATURE_SENSOR): TEMPERATURE_SENSOR_SCHEMA,
        # Optional HA number entities for runtime tuning
        cv.Optional(CONF_POWER_NUMBER): number.number_schema(MockHeaterNumber).extend(
            cv.COMPONENT_SCHEMA
        ),
        cv.Optional(CONF_THERMAL_MASS_NUMBER): number.number_schema(
            MockHeaterNumber
        ).extend(cv.COMPONENT_SCHEMA),
        cv.Optional(CONF_HEAT_LOSS_NUMBER): number.number_schema(
            MockHeaterNumber
        ).extend(cv.COMPONENT_SCHEMA),
        cv.Optional(CONF_AMBIENT_NUMBER): number.number_schema(
            MockHeaterNumber
        ).extend(cv.COMPONENT_SCHEMA),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Set initial values
    cg.add(var.set_initial_temperature(config[CONF_INITIAL_TEMPERATURE]))
    cg.add(var.set_ambient_temperature(config[CONF_AMBIENT_TEMPERATURE]))
    cg.add(var.set_power_watts(config[CONF_POWER_WATTS]))
    cg.add(var.set_thermal_mass(config[CONF_THERMAL_MASS_J_PER_C]))
    cg.add(var.set_heat_loss(config[CONF_HEAT_LOSS_W_PER_C]))
    cg.add(var.set_water_inlet_temp(config[CONF_WATER_INLET_TEMP_C]))

    # Create and register the output sub-entity
    out_conf = config[CONF_OUTPUT]
    out_var = cg.new_Pvariable(out_conf[CONF_ID])
    await cg.register_component(out_var, out_conf)
    await output.register_output(out_var, out_conf)
    cg.add(var.set_output(out_var))
    cg.add(out_var.set_parent(var))

    # Create and register the temperature sensor sub-entity
    sens_conf = config[CONF_TEMPERATURE_SENSOR]
    sens_var = await sensor.new_sensor(sens_conf)
    await cg.register_component(sens_var, sens_conf)
    cg.add(var.set_temperature_sensor(sens_var))
    cg.add(sens_var.set_parent(var))

    # Optional runtime-tunable number entities
    if CONF_POWER_NUMBER in config:
        num_conf = config[CONF_POWER_NUMBER]
        num_var = await number.new_number(
            num_conf, min_value=0.0, max_value=5000.0, step=10.0
        )
        await cg.register_component(num_var, num_conf)
        cg.add(var.set_power_number(num_var))
        cg.add(num_var.set_parent(var))

    if CONF_THERMAL_MASS_NUMBER in config:
        num_conf = config[CONF_THERMAL_MASS_NUMBER]
        num_var = await number.new_number(
            num_conf, min_value=100.0, max_value=10000.0, step=10.0
        )
        await cg.register_component(num_var, num_conf)
        cg.add(var.set_thermal_mass_number(num_var))
        cg.add(num_var.set_parent(var))

    if CONF_HEAT_LOSS_NUMBER in config:
        num_conf = config[CONF_HEAT_LOSS_NUMBER]
        num_var = await number.new_number(
            num_conf, min_value=0.0, max_value=20.0, step=0.1
        )
        await cg.register_component(num_var, num_conf)
        cg.add(var.set_heat_loss_number(num_var))
        cg.add(num_var.set_parent(var))

    if CONF_AMBIENT_NUMBER in config:
        num_conf = config[CONF_AMBIENT_NUMBER]
        num_var = await number.new_number(
            num_conf, min_value=-20.0, max_value=50.0, step=0.5
        )
        await cg.register_component(num_var, num_conf)
        cg.add(var.set_ambient_number(num_var))
        cg.add(num_var.set_parent(var))
