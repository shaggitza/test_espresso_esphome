# Espresso Shot History — HA Add-on

Records every espresso shot you pull and powers the **Shot History** view in
the Espresso Machine dashboard — no AppDaemon or SSH required.

## What it does

- Watches the ESPHome `machine_mode` sensor for `brewing → idle` transitions.
- Saves each shot (duration, yield, temperature) to `/data/shots.json` and to
  the `input_text.espresso_shot_log` HA helper.
- Keeps the `input_select.espresso_shot_viewer` dropdown up to date.
- Updates the `input_datetime` helpers whenever you pick a shot so the
  ApexCharts graphs snap to that shot's time window.

## Prerequisites

Before starting this add-on you need the four HA helper entities.  
Add `home_assistant/espresso_package.yaml` from the repository as an HA
package and restart HA — see the repository's `CUSTOM_UI.md` for details.

## Configuration

| Option | Default | Description |
|---|---|---|
| `mode_entity` | `text_sensor.philips_barista_brew_machine_mode` | ESPHome machine mode sensor |
| `duration_entity` | `sensor.philips_barista_brew_last_shot_duration` | Shot duration sensor |
| `yield_entity` | `sensor.philips_barista_brew_last_shot_yield` | Shot yield sensor (mL) |
| `temp_entity` | `sensor.philips_barista_brew_thermoblock_temperature` | Temperature sensor |
| `max_shots` | `20` | Maximum shots kept in the rolling log |
| `graph_padding_sec` | `15` | Seconds of padding on each side of the shot graph window |

If your ESPHome device is named `philips_barista_brew_mock` (the simulation
device) change every entity prefix to `philips_barista_brew_mock`.

## Support

Repository: <https://github.com/shaggitza/test_espresso_esphome>
