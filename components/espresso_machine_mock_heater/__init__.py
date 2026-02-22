import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import output, sensor, number
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    STATE_CLASS_MEASUREMENT,
)

CODEOWNERS = ["@shaggitza"]
AUTO_LOAD = ["output", "sensor", "number"]

espresso_machine_mock_heater_ns = cg.esphome_ns.namespace(
    "espresso_machine_mock_heater"
)
MockHeater = espresso_machine_mock_heater_ns.class_(
    "MockHeater", output.FloatOutput, cg.Component
)
MockHeaterParamNumber = espresso_machine_mock_heater_ns.class_(
    "MockHeaterParamNumber", number.Number
)
MockHeaterParam = espresso_machine_mock_heater_ns.enum("MockHeaterParam")

# ---------------------------------------------------------------------------
# Configuration key constants
# ---------------------------------------------------------------------------
CONF_INITIAL_TEMP = "initial_temp"
CONF_POWER_WATTS = "power_watts"
CONF_THERMAL_MASS_J_PER_C = "thermal_mass_j_per_c"
CONF_HEAT_LOSS_W_PER_C = "heat_loss_w_per_c"
CONF_AMBIENT_TEMP = "ambient_temp"
CONF_TEMPERATURE_SENSOR = "temperature_sensor"
CONF_PARAMS = "params"
# Keys used inside the params: sub-block (shorter names, no units suffix)
CONF_PARAM_THERMAL_MASS = "thermal_mass"
CONF_PARAM_HEAT_LOSS = "heat_loss"

# ---------------------------------------------------------------------------
# HA-adjustable parameter number schema
# ---------------------------------------------------------------------------
_PARAM_NUMBER_SCHEMA = number.number_schema(MockHeaterParamNumber)

PARAMS_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_POWER_WATTS): _PARAM_NUMBER_SCHEMA,
        cv.Optional(CONF_PARAM_THERMAL_MASS): _PARAM_NUMBER_SCHEMA,
        cv.Optional(CONF_PARAM_HEAT_LOSS): _PARAM_NUMBER_SCHEMA,
        cv.Optional(CONF_AMBIENT_TEMP): _PARAM_NUMBER_SCHEMA,
    }
)

# ---------------------------------------------------------------------------
# Temperature sensor sub-schema
# ---------------------------------------------------------------------------
TEMPERATURE_SENSOR_SCHEMA = sensor.sensor_schema(
    unit_of_measurement="°C",
    accuracy_decimals=1,
    state_class=STATE_CLASS_MEASUREMENT,
)

# ---------------------------------------------------------------------------
# Top-level component schema
# ---------------------------------------------------------------------------
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MockHeater),
        cv.Optional(CONF_NAME): cv.string,
        cv.Optional(CONF_INITIAL_TEMP, default=20.0): cv.float_,
        cv.Optional(CONF_POWER_WATTS, default=1200.0): cv.positive_float,
        cv.Optional(CONF_THERMAL_MASS_J_PER_C, default=1256.0): cv.positive_float,
        cv.Optional(CONF_HEAT_LOSS_W_PER_C, default=1.7): cv.positive_float,
        cv.Optional(CONF_AMBIENT_TEMP, default=20.0): cv.float_,
        cv.Required(CONF_TEMPERATURE_SENSOR): TEMPERATURE_SENSOR_SCHEMA,
        cv.Optional(CONF_PARAMS): PARAMS_SCHEMA,
    }
).extend(cv.COMPONENT_SCHEMA)


# ---------------------------------------------------------------------------
# Code generation
# ---------------------------------------------------------------------------
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await output.register_output(var, config)

    cg.add(var.set_initial_temp(config[CONF_INITIAL_TEMP]))
    cg.add(var.set_power_watts(config[CONF_POWER_WATTS]))
    cg.add(var.set_thermal_mass_j_per_c(config[CONF_THERMAL_MASS_J_PER_C]))
    cg.add(var.set_heat_loss_w_per_c(config[CONF_HEAT_LOSS_W_PER_C]))
    cg.add(var.set_ambient_temp(config[CONF_AMBIENT_TEMP]))

    # Temperature sensor — child entity owned by MockHeater
    sens = await sensor.new_sensor(config[CONF_TEMPERATURE_SENSOR])
    cg.add(var.set_temperature_sensor(sens))

    # HA-adjustable parameter number entities (all optional)
    if CONF_PARAMS in config:
        params = config[CONF_PARAMS]

        if CONF_POWER_WATTS in params:
            num = await number.new_number(
                params[CONF_POWER_WATTS],
                min_value=100.0,
                max_value=3000.0,
                step=50.0,
            )
            cg.add(num.set_parent(
                var,
                cg.RawExpression(
                    "espresso_machine_mock_heater::MockHeaterParam::POWER_WATTS"
                ),
            ))
            cg.add(var.set_power_number(num))

        if CONF_PARAM_THERMAL_MASS in params:
            num = await number.new_number(
                params[CONF_PARAM_THERMAL_MASS],
                min_value=10.0,
                max_value=5000.0,
                step=10.0,
            )
            cg.add(num.set_parent(
                var,
                cg.RawExpression(
                    "espresso_machine_mock_heater::MockHeaterParam::THERMAL_MASS"
                ),
            ))
            cg.add(var.set_thermal_mass_number(num))

        if CONF_PARAM_HEAT_LOSS in params:
            num = await number.new_number(
                params[CONF_PARAM_HEAT_LOSS],
                min_value=0.1,
                max_value=50.0,
                step=0.1,
            )
            cg.add(num.set_parent(
                var,
                cg.RawExpression(
                    "espresso_machine_mock_heater::MockHeaterParam::HEAT_LOSS"
                ),
            ))
            cg.add(var.set_heat_loss_number(num))

        if CONF_AMBIENT_TEMP in params:
            num = await number.new_number(
                params[CONF_AMBIENT_TEMP],
                min_value=-10.0,
                max_value=50.0,
                step=0.5,
            )
            cg.add(num.set_parent(
                var,
                cg.RawExpression(
                    "espresso_machine_mock_heater::MockHeaterParam::AMBIENT_TEMP"
                ),
            ))
            cg.add(var.set_ambient_temp_number(num))
