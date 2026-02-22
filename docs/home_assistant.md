# Home Assistant Dashboard — Espresso Machine

Below is a ready-to-paste Lovelace dashboard card configuration for the espresso machine.
Add it via **Settings → Dashboards → Edit Dashboard → Add Card → Manual**.

## Espresso Machine Card (Entities)

```yaml
type: entities
title: ☕ Espresso Machine
entities:
  - entity: climate.philips_barista_brew_main_heater
    name: Heater
  - entity: sensor.philips_barista_brew_brew_flow_total_volume
    name: Shot Volume (ml)
  - entity: sensor.philips_barista_brew_brew_flow_flow_rate
    name: Flow Rate (ml/s)
  - entity: number.philips_barista_brew_grind_time
    name: Grind Time (s)
  - entity: switch.philips_barista_brew_brew_valve
    name: Brew Valve
  - entity: switch.philips_barista_brew_steam_valve
    name: Steam Valve
  - entity: switch.philips_barista_brew_purge_valve
    name: Purge Valve
  - entity: switch.philips_barista_brew_pump
    name: Pump
  - entity: button.philips_barista_brew_restart
    name: Restart ESP32
```

## Temperature Graph Card

```yaml
type: history-graph
title: Thermoblock Temperature
entities:
  - entity: sensor.philips_barista_brew_main_heater_temperature
    name: Temperature
hours_to_show: 1
refresh_interval: 10
```

## Shot Stats Card (Last Shot)

```yaml
type: glance
title: Last Shot
entities:
  - entity: sensor.philips_barista_brew_brew_flow_total_volume
    name: Volume
  - entity: sensor.philips_barista_brew_shot_duration
    name: Time
  - entity: sensor.philips_barista_brew_main_heater_temperature
    name: Temp at Pull
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
        entity_id: sensor.philips_barista_brew_main_heater_temperature
        above: 88
    action:
      - service: notify.mobile_app
        data:
          title: "☕ Espresso ready"
          message: "Machine is at brew temperature."
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
