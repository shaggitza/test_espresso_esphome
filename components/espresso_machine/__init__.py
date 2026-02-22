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
CONF_PUMP = "pump"
CONF_FLOW_METER = "flow_meter"
CONF_VALVE = "valve"
CONF_PURGE_VALVE = "purge_valve"
CONF_TARGET_TEMPERATURE = "target_temperature"
CONF_FLOW_MAX = "flow_max"
CONF_FLOW_OFFSET = "flow_offset"
CONF_COOL_DOWN_TO = "cool_down_to"

# Validator for ml volumes (e.g. "40ml")
_validate_volume_ml = cv.float_with_unit("volume", "ml")
# Validator for ml/s flow rates (e.g. "2ml/s")
_validate_flow_rate = cv.float_with_unit("flow rate", "ml/s")

BREW_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_HEATER): cv.use_id(cg.Component),
        cv.Required(CONF_PUMP): cv.use_id(cg.Component),
        cv.Required(CONF_FLOW_METER): cv.use_id(cg.Component),
        cv.Required(CONF_VALVE): cv.use_id(cg.Component),
        cv.Required(CONF_PURGE_VALVE): cv.use_id(cg.Component),
        cv.Required(CONF_TARGET_TEMPERATURE): cv.temperature,
        cv.Required(CONF_FLOW_MAX): _validate_volume_ml,
        cv.Required(CONF_FLOW_OFFSET): _validate_volume_ml,
        # Advanced fields validated in later phases; accepted here to avoid errors
        cv.Optional("temperature_profile"): cv.Any(),
        cv.Optional("pre_infusion"): cv.Any(),
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
