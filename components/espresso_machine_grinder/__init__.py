import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation, pins
from esphome.components import button, number
from esphome.const import CONF_ID, CONF_PIN, CONF_TYPE

CODEOWNERS = ["@shaggitza"]
MULTI_CONF = True

espresso_machine_grinder_ns = cg.esphome_ns.namespace("espresso_machine_grinder")
Grinder = espresso_machine_grinder_ns.class_("Grinder", button.Button, cg.Component)
GrinderTimeNumber = espresso_machine_grinder_ns.class_(
    "GrinderTimeNumber", number.Number
)
GrindAction = espresso_machine_grinder_ns.class_("GrindAction", automation.Action)

CONF_DEFAULT_GRIND_TIME = "default_grind_time"
CONF_GRIND_TIME_NUMBER = "grind_time_number"
CONF_DURATION_MS = "duration_ms"

CONFIG_SCHEMA = (
    button.button_schema(Grinder)
    .extend(
        {
            cv.Required(CONF_TYPE): cv.one_of("relay", "none", lower=True),
            cv.Optional(CONF_PIN): pins.gpio_output_pin_schema,
            cv.Optional(
                CONF_DEFAULT_GRIND_TIME, default="7s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_GRIND_TIME_NUMBER): number.number_schema(
                GrinderTimeNumber,
                unit_of_measurement="ms",
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)

GRIND_ACTION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.use_id(Grinder),
        cv.Optional(CONF_DURATION_MS, default="0s"): cv.templatable(
            cv.positive_time_period_milliseconds
        ),
    }
)


@automation.register_action(
    "espresso_machine_grinder.grind", GrindAction, GRIND_ACTION_SCHEMA
)
async def grind_action_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    template_ = await cg.templatable(config[CONF_DURATION_MS], args, cg.uint32)
    cg.add(var.set_duration_ms(template_))
    return var


async def to_code(config):
    var = await button.new_button(config)
    await cg.register_component(var, config)

    grinder_type = config[CONF_TYPE]
    if grinder_type == "relay":
        cg.add(var.set_grinder_type(espresso_machine_grinder_ns.GrinderType.RELAY))
    else:
        cg.add(var.set_grinder_type(espresso_machine_grinder_ns.GrinderType.NONE))

    cg.add(
        var.set_default_grind_time(config[CONF_DEFAULT_GRIND_TIME].total_milliseconds)
    )

    if CONF_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_PIN])
        cg.add(var.set_pin(pin))

    if CONF_GRIND_TIME_NUMBER in config:
        num = await number.new_number(
            config[CONF_GRIND_TIME_NUMBER],
            min_value=500.0,
            max_value=30000.0,
            step=100.0,
        )
        cg.add(var.set_grind_time_number(num))
