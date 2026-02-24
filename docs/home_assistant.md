# Home Assistant Dashboard — Espresso Machine

Below is a ready-to-paste Lovelace dashboard card configuration for the espresso machine.
Add it via **Settings → Dashboards → Edit Dashboard → Add Card → Manual**.

## Entity Reference

After flashing, these entities will appear in HA (prefix: `philips_barista_brew`):

| Domain | Entity | Description |
|---|---|---|
| `climate` | `main_heater` | PID heater — set any target temperature directly from HA |
| `text_sensor` | `machine_mode` | Current machine state: `idle` / `brewing` / `steaming` |
| `button` | `brew_start` | Start an espresso shot sequence |
| `button` | `brew_stop` | Abort the current shot immediately |
| `button` | `steam_start` | Start a steam / milk-froth sequence |
| `button` | `steam_stop` | Stop steaming and begin cool-down |
| `button` | `grinder` | Trigger a one-shot timed grind |
| `number` | `grind_time` | Adjust grind duration (ms) from HA |
| `sensor` | `brew_flow_rate` | Live flow rate (mL/s) during extraction |
| `sensor` | `brew_flow_total` | Accumulated volume this shot (mL) |
| `sensor` | `last_shot_duration` | Duration of the last completed shot (s) |
| `sensor` | `last_shot_yield` | Net yield of the last shot = volume − puck absorption (mL) |
| `sensor` | `thermoblock_temperature` | Live thermoblock temperature (°C) |
| `switch` | `brew_valve` | Brew solenoid valve (direct control) |
| `switch` | `steam_valve` | Steam solenoid valve (direct control) |
| `switch` | `purge_valve` | Purge / drain solenoid valve (direct control) |
| `switch` | `vibration_pump` | Pump relay (direct control) |
| `button` | `restart` | Restart the ESP32 |
| `button` | `pid_autotune` | Run PID autotune — see `docs/pid_tuning.md` |

> **Tip — setting temperature above 90 °C:** Use the `climate.main_heater` entity card in HA.
> Click the temperature dial and drag it to any value (e.g. 95 °C for lighter roasts or 135 °C
> for steam). The PID controller will track the new setpoint immediately.

---

## Main Control Card

```yaml
type: entities
title: ☕ Espresso Machine
entities:
  - entity: text_sensor.philips_barista_brew_machine_mode
    name: Machine State
  - entity: climate.philips_barista_brew_main_heater
    name: Heater (set temperature here)
  - type: divider
  - entity: button.philips_barista_brew_brew_start
    name: ▶ Brew Start
  - entity: button.philips_barista_brew_brew_stop
    name: ■ Brew Stop
  - entity: button.philips_barista_brew_steam_start
    name: ▶ Steam Start
  - entity: button.philips_barista_brew_steam_stop
    name: ■ Steam Stop
  - type: divider
  - entity: button.philips_barista_brew_grinder
    name: Grind
  - entity: number.philips_barista_brew_grind_time
    name: Grind Time (ms)
  - type: divider
  - entity: sensor.philips_barista_brew_brew_flow_rate
    name: Flow Rate (mL/s)
  - entity: sensor.philips_barista_brew_brew_flow_total
    name: Shot Volume (mL)
  - entity: sensor.philips_barista_brew_last_shot_duration
    name: Last Shot Duration (s)
  - entity: sensor.philips_barista_brew_last_shot_yield
    name: Last Shot Yield (mL)
  - type: divider
  - entity: switch.philips_barista_brew_brew_valve
    name: Brew Valve
  - entity: switch.philips_barista_brew_steam_valve
    name: Steam Valve
  - entity: switch.philips_barista_brew_purge_valve
    name: Purge Valve
  - entity: switch.philips_barista_brew_vibration_pump
    name: Pump
  - type: divider
  - entity: button.philips_barista_brew_restart
    name: Restart ESP32
```

## Temperature Graph Card

```yaml
type: history-graph
title: Thermoblock Temperature
entities:
  - entity: sensor.philips_barista_brew_thermoblock_temperature
    name: Temperature
hours_to_show: 1
refresh_interval: 10
```

## Shot Stats Card (Last Shot)

```yaml
type: glance
title: Last Shot
entities:
  - entity: sensor.philips_barista_brew_brew_flow_total
    name: Volume (mL)
  - entity: sensor.philips_barista_brew_last_shot_duration
    name: Time (s)
  - entity: sensor.philips_barista_brew_last_shot_yield
    name: Yield (mL)
  - entity: sensor.philips_barista_brew_thermoblock_temperature
    name: Temp (°C)
```

---

## Useful Automations

### Auto-notify when machine is ready (at brew temperature)

```yaml
automation:
  - alias: "Espresso ready"
    trigger:
      - platform: state
        entity_id: climate.philips_barista_brew_main_heater
        to: "heat"
        for: "00:00:30"
    condition:
      - condition: numeric_state
        entity_id: sensor.philips_barista_brew_thermoblock_temperature
        above: 88
    action:
      - service: notify.mobile_app
        data:
          title: "☕ Espresso ready"
          message: "Machine is at brew temperature."
```

### Start a brew from HA (e.g. via Alexa / Google Assistant)

The `brew_start` button is a standard HA button entity. You can call it via:
- A dashboard button card press
- A voice assistant ("Hey Google, press brew start on the espresso machine")
- An HA script or automation action:

```yaml
action:
  - service: button.press
    target:
      entity_id: button.philips_barista_brew_brew_start
```

### Auto cool-down after inactivity

```yaml
automation:
  - alias: "Espresso auto off after 30 min idle"
    trigger:
      - platform: state
        entity_id: climate.philips_barista_brew_main_heater
        to: "heat"
        for: "00:30:00"
    action:
      - service: climate.set_hvac_mode
        target:
          entity_id: climate.philips_barista_brew_main_heater
        data:
          hvac_mode: "off"
```

### Log shot stats after each brew

```yaml
automation:
  - alias: "Log shot stats"
    trigger:
      - platform: state
        entity_id: text_sensor.philips_barista_brew_machine_mode
        from: "brewing"
        to: "idle"
    action:
      - service: notify.mobile_app
        data:
          title: "☕ Shot complete"
          message: >
            Yield: {{ states('sensor.philips_barista_brew_last_shot_yield') }} mL
            in {{ states('sensor.philips_barista_brew_last_shot_duration') }} s
```


---

## Combining with ESPHome `script:` Platform (P2-3)

The `espresso_machine:` orchestrator exposes two lifecycle callback hooks:

- `cleanup_script:` in the `brew:` section — fires when the brew sequence enters `CLEANUP` (after flow_max reached)
- `cleanup_script:` in the `steam:` section — fires when the steam sequence enters `CLEANUP` (after cool-down)

These callbacks integrate naturally with the native ESPHome `script:` platform.
Reference a script block by its `id:` in the `cleanup_script:` field to run any
sequence of ESPHome actions automatically after each shot or steam session:

```yaml
# Define a rinse script in the top-level scripts block
script:
  - id: brew_group_rinse
    sequence:
      - delay: 2s
      - switch.turn_on: purge_valve
      - delay: 3s
      - switch.turn_off: purge_valve

# Reference it in the orchestrator
espresso_machine:
  id: my_espresso
  brew:
    # ... other brew config ...
    cleanup_script: brew_group_rinse   # runs automatically after each shot
```

### Group-head Flush (espresso_machine.flush action)

For on-demand rinsing (e.g. via a dashboard button) use the built-in
`espresso_machine.flush` action, which pumps a configured volume through the
brew purge valve:

```yaml
button:
  - platform: template
    name: "Flush Group Head"
    on_press:
      - espresso_machine.flush:
          id: my_espresso
          volume_ml: 50ml   # pump 50 mL through the purge valve
```

The `flush` action is only accepted when the machine is idle and powered on.
It uses the `brew_purge_valve` and `brew_pump` configured in the `brew:` section.

### Combining script: + flush for a full post-shot routine

The `espresso_machine.flush` action is an ESPHome action, so it is called from ESPHome
scripts. Home Assistant notifications can be triggered using the `homeassistant.service:` action.

```yaml
# ESPHome script that flushes the group head and then notifies via Home Assistant
script:
  - id: post_shot_routine
    sequence:
      # 1. Flush 30 mL through the group head (ESPHome action)
      - espresso_machine.flush:
          id: my_espresso
          volume_ml: 30ml
      # 2. Notify via Home Assistant service (requires HA API connection)
      - homeassistant.service:
          service: notify.mobile_app
          data:
            title: "☕ Shot complete"
            message: "Group head flushed and ready for next shot."
```

You can trigger `post_shot_routine` from a Home Assistant automation that watches for the
`text_sensor.machine_mode` to return to `idle`, or attach it as an ESPHome `on_` trigger.
