import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import binary_sensor, display, sensor
from esphome.const import CONF_ID

CODEOWNERS = ["@shaggitza"]
DEPENDENCIES = ["display", "sensor", "binary_sensor"]
AUTO_LOAD = ["sensor", "binary_sensor"]

# ---------------------------------------------------------------------------
# Namespace + class declarations
# ---------------------------------------------------------------------------
espresso_machine_display_ns = cg.esphome_ns.namespace("espresso_machine_display")
EspressoMachineDisplay = espresso_machine_display_ns.class_(
    "EspressoMachineDisplay", cg.Component
)
Theme = espresso_machine_display_ns.enum("Theme")

# ---------------------------------------------------------------------------
# Cross-component type references (used only for id resolution, no header import)
# ---------------------------------------------------------------------------
_espresso_machine_ns = cg.esphome_ns.namespace("espresso_machine")
EspressoMachine = _espresso_machine_ns.class_("EspressoMachine", cg.Component)

_grinder_ns = cg.esphome_ns.namespace("espresso_machine_grinder")
Grinder = _grinder_ns.class_("Grinder", cg.Component)

_heater_ns = cg.esphome_ns.namespace("espresso_machine_heater")
EspressoMachineHeater = _heater_ns.class_("EspressoMachineHeater", cg.Component)

_flow_meter_ns = cg.esphome_ns.namespace("espresso_machine_flow_meter")
FlowMeter = _flow_meter_ns.class_("FlowMeter", cg.Component)

# Font type — fonts created by the 'font:' platform are font::Font objects
_font_ns = cg.esphome_ns.namespace("font")
Font = _font_ns.class_("Font")

# ---------------------------------------------------------------------------
# Config key constants
# ---------------------------------------------------------------------------
CONF_DISPLAY_ID = "display_id"
CONF_ENCODER_ID = "encoder_id"
CONF_BUTTON_ID = "button_id"
CONF_ESPRESSO_MACHINE = "espresso_machine"
CONF_GRINDER = "grinder"
CONF_HEATER = "heater"
CONF_FLOW_METER = "flow_meter"
CONF_THEME = "theme"
CONF_LONG_PRESS_MS = "long_press_ms"
CONF_SCREENSAVER_TIMEOUT = "screensaver_timeout"
CONF_BEEPER_PIN = "beeper_pin"
CONF_HOME_ACTIONS = "home_actions"
CONF_FONT_SMALL = "font_small"
CONF_FONT_LARGE = "font_large"

# ---------------------------------------------------------------------------
# Theme name → C++ enum value
# ---------------------------------------------------------------------------
THEME_LOOKUP = {
    "classic": "espresso_machine_display::Theme::CLASSIC",
    "minimal": "espresso_machine_display::Theme::MINIMAL",
    "barista": "espresso_machine_display::Theme::BARISTA",
    "dark": "espresso_machine_display::Theme::DARK",
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EspressoMachineDisplay),
        # ── Hardware bindings (ALL THREE REQUIRED) ────────────────────────────
        cv.Required(CONF_DISPLAY_ID): cv.use_id(display.DisplayBuffer),
        cv.Required(CONF_ENCODER_ID): cv.use_id(sensor.Sensor),
        cv.Required(CONF_BUTTON_ID): cv.use_id(binary_sensor.BinarySensor),
        # ── Machine orchestrator (required) ───────────────────────────────────
        cv.Required(CONF_ESPRESSO_MACHINE): cv.use_id(EspressoMachine),
        # ── Optional sub-component bindings ──────────────────────────────────
        # When bound the corresponding actions/data appear in the UI automatically.
        cv.Optional(CONF_GRINDER): cv.use_id(Grinder),
        # Concrete types so the C++ setter receives the right typed pointer
        # (EspressoMachineHeater* / FlowMeter*) — both implement IHeater / IFlowMeter.
        cv.Optional(CONF_HEATER): cv.use_id(EspressoMachineHeater),
        cv.Optional(CONF_FLOW_METER): cv.use_id(FlowMeter),
        # ── Fonts (optional) ──────────────────────────────────────────────────
        # When omitted, text is rendered using the fall-back 5×7 built-in font.
        # Declare fonts in the standard ESPHome font: platform block and
        # reference their ids here.
        cv.Optional(CONF_FONT_SMALL): cv.use_id(Font),
        cv.Optional(CONF_FONT_LARGE): cv.use_id(Font),
        # ── Visual theme ──────────────────────────────────────────────────────
        cv.Optional(CONF_THEME, default="classic"): cv.one_of(
            "classic", "minimal", "barista", "dark", lower=True
        ),
        # ── Behaviour ─────────────────────────────────────────────────────────
        cv.Optional(
            CONF_LONG_PRESS_MS, default="1000ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_SCREENSAVER_TIMEOUT, default="5min"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_BEEPER_PIN): pins.gpio_output_pin_schema,
        # ── Home Screen quick-action order ────────────────────────────────────
        cv.Optional(
            CONF_HOME_ACTIONS,
            default=["brew", "steam", "grind", "settings", "power"],
        ): cv.ensure_list(
            cv.one_of("brew", "steam", "grind", "settings", "power", lower=True)
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Hardware bindings
    disp = await cg.get_variable(config[CONF_DISPLAY_ID])
    cg.add(var.set_display(disp))

    enc = await cg.get_variable(config[CONF_ENCODER_ID])
    cg.add(var.set_encoder(enc))

    btn = await cg.get_variable(config[CONF_BUTTON_ID])
    cg.add(var.set_button(btn))

    # Machine orchestrator
    machine = await cg.get_variable(config[CONF_ESPRESSO_MACHINE])
    cg.add(var.set_espresso_machine(machine))

    # Optional sub-component bindings
    if CONF_GRINDER in config:
        grinder = await cg.get_variable(config[CONF_GRINDER])
        cg.add(var.set_grinder(grinder))

    if CONF_HEATER in config:
        heater = await cg.get_variable(config[CONF_HEATER])
        cg.add(var.set_heater(heater))

    if CONF_FLOW_METER in config:
        fm = await cg.get_variable(config[CONF_FLOW_METER])
        cg.add(var.set_flow_meter(fm))

    # Fonts
    if CONF_FONT_SMALL in config:
        font_s = await cg.get_variable(config[CONF_FONT_SMALL])
        cg.add(var.set_font_small(font_s))

    if CONF_FONT_LARGE in config:
        font_l = await cg.get_variable(config[CONF_FONT_LARGE])
        cg.add(var.set_font_large(font_l))

    # Theme
    cg.add(
        var.set_theme(cg.RawExpression(THEME_LOOKUP[config[CONF_THEME]]))
    )

    # Behaviour
    cg.add(var.set_long_press_ms(config[CONF_LONG_PRESS_MS]))
    cg.add(var.set_screensaver_timeout_ms(config[CONF_SCREENSAVER_TIMEOUT]))

    if CONF_BEEPER_PIN in config:
        beeper = await cg.gpio_pin_expression(config[CONF_BEEPER_PIN])
        cg.add(var.set_beeper_pin(beeper))

    # Home action list (passed as a vector of string constants)
    actions = config[CONF_HOME_ACTIONS]
    for action in actions:
        cg.add(var.add_home_action(action))
