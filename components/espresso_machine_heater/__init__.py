import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, number
from esphome.const import CONF_ID, UNIT_MILLISECOND

CODEOWNERS = ["@shaggitza"]
DEPENDENCIES = ["climate"]
AUTO_LOAD = ["number"]

espresso_machine_heater_ns = cg.esphome_ns.namespace("espresso_machine_heater")
EspressoMachineHeater = espresso_machine_heater_ns.class_(
    "EspressoMachineHeater", cg.Component
)
SsrPeriodNumber = espresso_machine_heater_ns.class_(
    "SsrPeriodNumber", number.Number
)

# Reference SlowPWMOutput without importing the slow_pwm module.
# When ssr_output is configured, slow_pwm is already loaded (the user defines
# `output: - platform: slow_pwm` in their YAML), making its headers available
# during compilation.  No hard dependency is added here so that users who use
# a gpio/bang-bang output are not forced to include slow_pwm.
_slow_pwm_ns = cg.esphome_ns.namespace("slow_pwm")
SlowPWMOutput = _slow_pwm_ns.class_("SlowPWMOutput")

CONF_CLIMATE_ID = "climate_id"
CONF_TEMPERATURE_TOLERANCE = "temperature_tolerance"
CONF_SSR_OUTPUT = "ssr_output"
CONF_SSR_PERIOD_NUMBER = "ssr_period_number"
CONF_SSR_DEFAULT_PERIOD_MS = "ssr_default_period_ms"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EspressoMachineHeater),
        cv.Required(CONF_CLIMATE_ID): cv.use_id(climate.Climate),
        cv.Optional(CONF_TEMPERATURE_TOLERANCE, default=0.5): cv.float_range(min=0.0),
        # Optional SSR period control — wire the slow_pwm output here to expose
        # its switching period as a Home Assistant number entity.
        cv.Optional(CONF_SSR_OUTPUT): cv.use_id(SlowPWMOutput),
        cv.Optional(CONF_SSR_PERIOD_NUMBER): number.number_schema(
            SsrPeriodNumber,
            unit_of_measurement=UNIT_MILLISECOND,
        ),
        # Default period (ms) published to HA on boot.  Should match the period
        # configured on the slow_pwm output in YAML.  Default: 1000 ms (1 Hz).
        cv.Optional(CONF_SSR_DEFAULT_PERIOD_MS, default=1000): cv.positive_int,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    climate_entity = await cg.get_variable(config[CONF_CLIMATE_ID])
    cg.add(var.set_climate(climate_entity))
    cg.add(var.set_temperature_tolerance(config[CONF_TEMPERATURE_TOLERANCE]))

    if CONF_SSR_OUTPUT in config:
        ssr_out = await cg.get_variable(config[CONF_SSR_OUTPUT])
        # Generate a lambda that calls set_period() on the slow_pwm output when
        # the HA number entity changes.  The lambda is embedded in the main
        # generated .cpp file where all component headers are available.
        cg.add(var.set_ssr_period_callback(
            cg.RawExpression(f"[=](uint32_t ms) {{ {ssr_out}->set_period(ms); }}")
        ))

    cg.add(var.set_ssr_default_period_ms(config[CONF_SSR_DEFAULT_PERIOD_MS]))

    if CONF_SSR_PERIOD_NUMBER in config:
        num = await number.new_number(
            config[CONF_SSR_PERIOD_NUMBER],
            min_value=8.0,
            max_value=10000.0,
            step=1.0,
        )
        cg.add(num.set_parent(var))
        cg.add(var.set_ssr_period_number(num))
