import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_ID, CONF_NAME

CODEOWNERS = ["@shaggitza"]
AUTO_LOAD = ["number"]

espresso_machine_mock_pump_ns = cg.esphome_ns.namespace(
    "espresso_machine_mock_pump"
)
MockPump = espresso_machine_mock_pump_ns.class_(
    "MockPump", cg.Component
)
MockPumpParamNumber = espresso_machine_mock_pump_ns.class_(
    "MockPumpParamNumber", number.Number
)
MockPumpParam = espresso_machine_mock_pump_ns.enum("MockPumpParam")

# ---------------------------------------------------------------------------
# Configuration key constants
# ---------------------------------------------------------------------------
CONF_NOMINAL_FLOW_ML_PER_S = "nominal_flow_ml_per_s"
CONF_PUCK_TIME_CONSTANT_S = "puck_time_constant_s"
CONF_PARAMS = "params"
# Keys inside the params: sub-block (shorter names, no units suffix)
CONF_PARAM_NOMINAL_FLOW = "nominal_flow"
CONF_PARAM_PUCK_TIME_CONSTANT = "puck_time_constant"

# ---------------------------------------------------------------------------
# HA-adjustable parameter number schema
# ---------------------------------------------------------------------------
_PARAM_NUMBER_SCHEMA = number.number_schema(MockPumpParamNumber)

PARAMS_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_PARAM_NOMINAL_FLOW): _PARAM_NUMBER_SCHEMA,
        cv.Optional(CONF_PARAM_PUCK_TIME_CONSTANT): _PARAM_NUMBER_SCHEMA,
    }
)

# ---------------------------------------------------------------------------
# Top-level component schema
# ---------------------------------------------------------------------------
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MockPump),
        cv.Optional(CONF_NAME): cv.string,
        cv.Optional(CONF_NOMINAL_FLOW_ML_PER_S, default=4.0): cv.positive_float,
        cv.Optional(CONF_PUCK_TIME_CONSTANT_S, default=10.0): cv.positive_float,
        cv.Optional(CONF_PARAMS): PARAMS_SCHEMA,
    }
).extend(cv.COMPONENT_SCHEMA)


# ---------------------------------------------------------------------------
# Code generation
# ---------------------------------------------------------------------------
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_nominal_flow_ml_per_s(config[CONF_NOMINAL_FLOW_ML_PER_S]))
    cg.add(var.set_puck_time_constant_s(config[CONF_PUCK_TIME_CONSTANT_S]))

    # HA-adjustable parameter number entities (all optional)
    if CONF_PARAMS in config:
        params = config[CONF_PARAMS]

        if CONF_PARAM_NOMINAL_FLOW in params:
            num = await number.new_number(
                params[CONF_PARAM_NOMINAL_FLOW],
                min_value=0.5,
                max_value=10.0,
                step=0.1,
            )
            cg.add(num.set_parent(
                var,
                cg.RawExpression(
                    "espresso_machine_mock_pump::MockPumpParam::NOMINAL_FLOW"
                ),
            ))
            cg.add(var.set_nominal_flow_number(num))

        if CONF_PARAM_PUCK_TIME_CONSTANT in params:
            num = await number.new_number(
                params[CONF_PARAM_PUCK_TIME_CONSTANT],
                min_value=1.0,
                max_value=60.0,
                step=0.5,
            )
            cg.add(num.set_parent(
                var,
                cg.RawExpression(
                    "espresso_machine_mock_pump::MockPumpParam::PUCK_TAU"
                ),
            ))
            cg.add(var.set_puck_tau_number(num))
