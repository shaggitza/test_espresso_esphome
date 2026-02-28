import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import number, sensor, switch, text_sensor
from esphome.const import CONF_ID, UNIT_SECOND, STATE_CLASS_MEASUREMENT

CODEOWNERS = ["@shaggitza"]
AUTO_LOAD = ["number", "sensor", "switch", "text_sensor"]

espresso_machine_ns = cg.esphome_ns.namespace("espresso_machine")
EspressoMachine = espresso_machine_ns.class_("EspressoMachine", cg.Component)
BrewFlowMaxNumber = espresso_machine_ns.class_("BrewFlowMaxNumber", number.Number)
TempSurfSwitch = espresso_machine_ns.class_("TempSurfSwitch", switch.Switch)
FlushAction = espresso_machine_ns.class_("FlushAction", automation.Action)

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
CONF_FLOW_MAX_NUMBER = "flow_max_number"
CONF_FLOW_OFFSET = "flow_offset"
CONF_COOL_DOWN_TO = "cool_down_to"
CONF_TEMPERATURE_COOLDOWN = "temperature_cooldown"
CONF_PURGE_VOLUME = "purge_volume"
CONF_STEAM_TIMEOUT = "timeout"
CONF_FLUSH_VOLUME = "volume_ml"

# Temperature-surfing sub-schema keys
CONF_TEMPERATURE_PROFILE = "temperature_profile"
CONF_TEMP_OFFSET = "offset"
CONF_TEMP_RAMP_TIME = "ramp_time"
CONF_TEMP_SURF_SWITCH = "temp_surf_switch"

# Pre-infusion sub-schema keys
CONF_PRE_INFUSION = "pre_infusion"
CONF_PRE_INFUSION_ENABLED = "enabled"
CONF_PRE_INFUSION_VOLUME = "volume_ml"
CONF_PRE_INFUSION_HOLD_TIME = "hold_time"

# Shot-stats sensor sub-schema keys (P1-5)
CONF_SHOT_STATS = "shot_stats"
CONF_SHOT_TIME_SENSOR = "last_shot_time"
CONF_SHOT_VOLUME_SENSOR = "last_shot_volume"
CONF_SHOT_YIELD_SENSOR = "last_shot_yield"

# Status text sensor key
CONF_STATUS_SENSOR = "status_sensor"

# Power switch key
CONF_POWER_SWITCH = "power_switch"

# Idle auto-off timeout key
CONF_IDLE_TIMEOUT = "idle_timeout"

# Validator for ml volumes (e.g. "40ml")
_validate_volume_ml = cv.float_with_unit("volume", "ml")
# Validator for ml/s flow rates (e.g. "2ml/s")
_validate_flow_rate = cv.float_with_unit("flow rate", "ml/s")

TEMPERATURE_PROFILE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_TEMP_OFFSET): cv.temperature,
        cv.Required(CONF_TEMP_RAMP_TIME): cv.positive_time_period_milliseconds,
        # Optional HA switch that enables/disables temperature surfing at runtime.
        # When omitted, surfing is always active (governed by offset/ramp_time).
        cv.Optional(CONF_TEMP_SURF_SWITCH): switch.switch_schema(
            TempSurfSwitch,
        ),
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
        # Optional IHeater-implementing component for temperature-gated HEATING
        # transition and temperature-surfing setpoint application (P1-2, P1-3).
        cv.Optional(CONF_HEATER_CTRL): cv.use_id(cg.Component),
        cv.Required(CONF_PUMP): cv.use_id(cg.Component),
        cv.Required(CONF_VALVE): cv.use_id(cg.Component),
        cv.Required(CONF_PURGE_VALVE): cv.use_id(cg.Component),
        # target_temperature is optional: when omitted the heater setpoint set from
        # Home Assistant is used as-is and never overridden by the orchestrator.
        cv.Optional(CONF_TARGET_TEMPERATURE): cv.temperature,
        cv.Optional(CONF_TEMPERATURE_PROFILE): TEMPERATURE_PROFILE_SCHEMA,
        cv.Required(CONF_FLOW_MAX): _validate_volume_ml,
        cv.Required(CONF_FLOW_OFFSET): _validate_volume_ml,
        cv.Optional(CONF_FLOW_MAX_NUMBER): number.number_schema(
            BrewFlowMaxNumber,
            unit_of_measurement="mL",
        ),
        cv.Optional(CONF_PRE_INFUSION): PRE_INFUSION_SCHEMA,
        # When true, if brew_start() is called while the thermoblock is above
        # target_temperature (e.g. still hot after an aborted steam session),
        # the brew sequence inserts a COOLING state BEFORE the HEATING phase:
        # the purge valve opens, the pump runs in bypass mode, and the
        # orchestrator waits for the thermoblock to drop to target_temperature
        # before proceeding to HEATING.  Requires heater_controller: to be
        # wired; silently ignored if no heater controller is configured.
        cv.Optional(CONF_TEMPERATURE_COOLDOWN, default=False): cv.boolean,
        # Shot statistics exposed as HA sensor entities (P1-5)
        cv.Optional(CONF_SHOT_STATS): cv.Schema(
            {
                cv.Optional(CONF_SHOT_TIME_SENSOR): sensor.sensor_schema(
                    unit_of_measurement=UNIT_SECOND,
                    accuracy_decimals=1,
                    state_class=STATE_CLASS_MEASUREMENT,
                ),
                cv.Optional(CONF_SHOT_VOLUME_SENSOR): sensor.sensor_schema(
                    unit_of_measurement="mL",
                    accuracy_decimals=1,
                    state_class=STATE_CLASS_MEASUREMENT,
                ),
                cv.Optional(CONF_SHOT_YIELD_SENSOR): sensor.sensor_schema(
                    unit_of_measurement="mL",
                    accuracy_decimals=1,
                    state_class=STATE_CLASS_MEASUREMENT,
                ),
            }
        ),
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
        # Volume to pump through the purge valve before opening the steam valve.
        # Clears residual water so only dry steam reaches the wand.
        cv.Optional(CONF_PURGE_VOLUME): _validate_volume_ml,
        # Safety timeout: stop steaming after this duration (0 = disabled).
        cv.Optional(CONF_STEAM_TIMEOUT): cv.positive_time_period_milliseconds,
        # Advanced fields validated in later phases; accepted here to avoid errors
        cv.Optional("cleanup_script"): cv.Any(),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EspressoMachine),
        cv.Optional(CONF_BREW): BREW_SCHEMA,
        cv.Optional(CONF_STEAM): STEAM_SCHEMA,
        # Optional text sensor that reports a detailed status string to HA on
        # every state transition (e.g. "Brew: Heating", "Brewing", "Steam: Cooling").
        # More informative than the template machine-mode sensor.
        cv.Optional(CONF_STATUS_SENSOR): text_sensor.text_sensor_schema(),
        # Optional reference to the HA power switch entity.  When wired the
        # orchestrator publishes power state changes so the switch stays in sync
        # after internal power-off events (idle auto-off, deferred off after
        # steam cooldown).  Without this, the HA switch can show ON while the
        # machine is internally off.
        cv.Optional(CONF_POWER_SWITCH): cv.use_id(switch.Switch),
        # Auto power-off when idle for this duration.  Default: 30 min.  Set to
        # 0 to disable.  Adjustable only via YAML (not at runtime from HA).
        cv.Optional(CONF_IDLE_TIMEOUT, default="30min"): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    if CONF_STATUS_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_STATUS_SENSOR])
        cg.add(var.set_status_sensor(sens))

    if CONF_POWER_SWITCH in config:
        sw = await cg.get_variable(config[CONF_POWER_SWITCH])
        cg.add(var.set_power_switch(sw))

    cg.add(var.set_idle_timeout_ms(config[CONF_IDLE_TIMEOUT]))

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

        cg.add(var.set_brew_flow_max(brew[CONF_FLOW_MAX]))
        cg.add(var.set_brew_flow_offset(brew[CONF_FLOW_OFFSET]))

        if CONF_FLOW_MAX_NUMBER in brew:
            num = await number.new_number(
                brew[CONF_FLOW_MAX_NUMBER],
                min_value=10.0,
                max_value=200.0,
                step=1.0,
            )
            cg.add(num.set_parent(var))
            cg.add(var.set_brew_flow_max_number(num))

        if CONF_HEATER_CTRL in brew:
            heater_ctrl = await cg.get_variable(brew[CONF_HEATER_CTRL])
            cg.add(var.set_brew_heater_ctrl(heater_ctrl))

        if CONF_TARGET_TEMPERATURE in brew:
            cg.add(var.set_brew_target_temperature(brew[CONF_TARGET_TEMPERATURE]))

        if CONF_TEMPERATURE_PROFILE in brew:
            tp = brew[CONF_TEMPERATURE_PROFILE]
            cg.add(var.set_brew_temp_offset(tp[CONF_TEMP_OFFSET]))
            cg.add(var.set_brew_temp_ramp_time_ms(tp[CONF_TEMP_RAMP_TIME]))
            if CONF_TEMP_SURF_SWITCH in tp:
                sw = await switch.new_switch(tp[CONF_TEMP_SURF_SWITCH])
                cg.add(sw.set_parent(var))
                cg.add(var.set_temp_surf_switch(sw))

        if CONF_PRE_INFUSION in brew:
            pi = brew[CONF_PRE_INFUSION]
            cg.add(var.set_pre_infusion_enabled(pi[CONF_PRE_INFUSION_ENABLED]))
            cg.add(var.set_pre_infusion_volume_ml(pi[CONF_PRE_INFUSION_VOLUME]))
            cg.add(var.set_pre_infusion_hold_time_ms(pi[CONF_PRE_INFUSION_HOLD_TIME]))

        cg.add(var.set_brew_temperature_cooldown(brew[CONF_TEMPERATURE_COOLDOWN]))

        if CONF_SHOT_STATS in brew:
            stats = brew[CONF_SHOT_STATS]
            if CONF_SHOT_TIME_SENSOR in stats:
                sens = await sensor.new_sensor(stats[CONF_SHOT_TIME_SENSOR])
                cg.add(var.set_last_shot_time_sensor(sens))
            if CONF_SHOT_VOLUME_SENSOR in stats:
                sens = await sensor.new_sensor(stats[CONF_SHOT_VOLUME_SENSOR])
                cg.add(var.set_last_shot_volume_sensor(sens))
            if CONF_SHOT_YIELD_SENSOR in stats:
                sens = await sensor.new_sensor(stats[CONF_SHOT_YIELD_SENSOR])
                cg.add(var.set_last_shot_yield_sensor(sens))

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

        if CONF_PURGE_VOLUME in steam:
            cg.add(var.set_steam_purge_volume_ml(steam[CONF_PURGE_VOLUME]))

        if CONF_STEAM_TIMEOUT in steam:
            cg.add(var.set_steam_timeout_ms(steam[CONF_STEAM_TIMEOUT]))


# ---------------------------------------------------------------------------
# espresso_machine.flush action (P2-2)
# Usage in YAML:
#   - espresso_machine.flush:
#       id: my_espresso
#       volume_ml: 50ml
# ---------------------------------------------------------------------------
@automation.register_action(
    "espresso_machine.flush",
    FlushAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(EspressoMachine),
            cv.Required(CONF_FLUSH_VOLUME): _validate_volume_ml,
        }
    ),
)
async def flush_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_volume_ml(config[CONF_FLUSH_VOLUME]))
    return var
