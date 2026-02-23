import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@shaggitza"]

espresso_machine_ns = cg.esphome_ns.namespace("espresso_machine")
EspressoMachine = espresso_machine_ns.class_("EspressoMachine", cg.Component)

# Keys for brew sub-schema
CONF_BREW = "brew"
CONF_STEAM = "steam"
CONF_HEATER = "heater"
CONF_HEATER_CTRL = "heater_controller"
CONF_PUMP = "pump"
CONF_VALVE = "valve"
CONF_PURGE_VALVE = "purge_valve"
CONF_TARGET_TEMPERATURE = "target_temperature"
CONF_FLOW_MAX = "flow_max"
CONF_FLOW_OFFSET = "flow_offset"
CONF_COOL_DOWN_TO = "cool_down_to"

# Temperature-surfing sub-schema keys
CONF_TEMPERATURE_PROFILE = "temperature_profile"
CONF_TEMP_OFFSET = "offset"
CONF_TEMP_RAMP_TIME = "ramp_time"

# Pre-infusion sub-schema keys
CONF_PRE_INFUSION = "pre_infusion"
CONF_PRE_INFUSION_ENABLED = "enabled"
CONF_PRE_INFUSION_VOLUME = "volume_ml"
CONF_PRE_INFUSION_HOLD_TIME = "hold_time"

# Validator for ml volumes (e.g. "40ml")
_validate_volume_ml = cv.float_with_unit("volume", "ml")
# Validator for ml/s flow rates (e.g. "2ml/s")
_validate_flow_rate = cv.float_with_unit("flow rate", "ml/s")

TEMPERATURE_PROFILE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_TEMP_OFFSET): cv.temperature,
        cv.Required(CONF_TEMP_RAMP_TIME): cv.positive_time_period_milliseconds,
    }
)

PRE_INFUSION_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_PRE_INFUSION_ENABLED, default=False): cv.boolean,
        cv.Required(CONF_PRE_INFUSION_VOLUME): _validate_volume_ml,
        cv.Required(CONF_PRE_INFUSION_HOLD_TIME): cv.positive_time_period_milliseconds,
    }
)

BREW_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_HEATER): cv.use_id(cg.Component),
        cv.Required(CONF_PUMP): cv.use_id(cg.Component),
        cv.Required(CONF_VALVE): cv.use_id(cg.Component),
        cv.Required(CONF_PURGE_VALVE): cv.use_id(cg.Component),
        cv.Required(CONF_TARGET_TEMPERATURE): cv.temperature,
        cv.Optional(CONF_TEMPERATURE_PROFILE): TEMPERATURE_PROFILE_SCHEMA,
        cv.Required(CONF_FLOW_MAX): _validate_volume_ml,
        cv.Required(CONF_FLOW_OFFSET): _validate_volume_ml,
        cv.Optional(CONF_PRE_INFUSION): PRE_INFUSION_SCHEMA,
        # cleanup_script wired in Phase 9
        cv.Optional("cleanup_script"): cv.Any(),
    }
)

STEAM_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_HEATER): cv.use_id(cg.Component),
        cv.Required(CONF_PUMP): cv.use_id(cg.Component),
        cv.Required(CONF_VALVE): cv.use_id(cg.Component),
        cv.Required(CONF_PURGE_VALVE): cv.use_id(cg.Component),
        cv.Required(CONF_TARGET_TEMPERATURE): cv.temperature,
        cv.Required(CONF_FLOW_MAX): _validate_flow_rate,
        cv.Required(CONF_COOL_DOWN_TO): cv.temperature,
        # Optional IHeater-implementing component for temperature-gated transitions
        cv.Optional(CONF_HEATER_CTRL): cv.use_id(cg.Component),
        # Advanced fields validated in later phases; accepted here to avoid errors
        cv.Optional("cleanup_script"): cv.Any(),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EspressoMachine),
        cv.Optional(CONF_BREW): BREW_SCHEMA,
        cv.Optional(CONF_STEAM): STEAM_SCHEMA,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    if CONF_BREW in config:
        brew = config[CONF_BREW]

        heater = await cg.get_variable(brew[CONF_HEATER])
        cg.add(var.set_brew_heater(heater))

        pump = await cg.get_variable(brew[CONF_PUMP])
        cg.add(var.set_brew_pump(pump))

        valve = await cg.get_variable(brew[CONF_VALVE])
        cg.add(var.set_brew_valve(valve))

        purge_valve = await cg.get_variable(brew[CONF_PURGE_VALVE])
        cg.add(var.set_brew_purge_valve(purge_valve))

        cg.add(var.set_brew_target_temperature(brew[CONF_TARGET_TEMPERATURE]))
        cg.add(var.set_brew_flow_max(brew[CONF_FLOW_MAX]))
        cg.add(var.set_brew_flow_offset(brew[CONF_FLOW_OFFSET]))

        if CONF_TEMPERATURE_PROFILE in brew:
            tp = brew[CONF_TEMPERATURE_PROFILE]
            cg.add(var.set_brew_temp_offset(tp[CONF_TEMP_OFFSET]))
            cg.add(var.set_brew_temp_ramp_time_ms(tp[CONF_TEMP_RAMP_TIME]))

        if CONF_PRE_INFUSION in brew:
            pi = brew[CONF_PRE_INFUSION]
            cg.add(var.set_pre_infusion_enabled(pi[CONF_PRE_INFUSION_ENABLED]))
            cg.add(var.set_pre_infusion_volume_ml(pi[CONF_PRE_INFUSION_VOLUME]))
            cg.add(var.set_pre_infusion_hold_time_ms(pi[CONF_PRE_INFUSION_HOLD_TIME]))

    if CONF_STEAM in config:
        steam = config[CONF_STEAM]

        heater = await cg.get_variable(steam[CONF_HEATER])
        cg.add(var.set_steam_heater(heater))

        pump = await cg.get_variable(steam[CONF_PUMP])
        cg.add(var.set_steam_pump(pump))

        valve = await cg.get_variable(steam[CONF_VALVE])
        cg.add(var.set_steam_valve(valve))

        purge_valve = await cg.get_variable(steam[CONF_PURGE_VALVE])
        cg.add(var.set_steam_purge_valve(purge_valve))

        cg.add(var.set_steam_target_temperature(steam[CONF_TARGET_TEMPERATURE]))
        cg.add(var.set_steam_flow_max(steam[CONF_FLOW_MAX]))
        cg.add(var.set_steam_cool_down_to(steam[CONF_COOL_DOWN_TO]))

        if CONF_HEATER_CTRL in steam:
            heater_ctrl = await cg.get_variable(steam[CONF_HEATER_CTRL])
            cg.add(var.set_steam_heater_ctrl(heater_ctrl))
