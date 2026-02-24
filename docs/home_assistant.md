# Home Assistant Dashboard — Espresso Machine

## Dashboards

Two ready-to-paste Lovelace dashboards are provided:

| File | Target | Description |
|---|---|---|
| `home_assistant/dashboards/dashboard.yaml` | Real hardware + simulation | Unified dashboard — Controls, Live Data, Advanced |
| `home_assistant/dashboards/dashboard_mock.yaml` | Simulation only | Dedicated mock dashboard — Controls, Live Data, Advanced |

**How to install:**
1. Settings → Dashboards → Add Dashboard
2. Name it (e.g. "Espresso Machine" or "Espresso Sim"), open it → three-dot menu → "Edit Dashboard"
3. Three-dot menu again → "Raw configuration editor"
4. Paste the full contents of the chosen file (starting from `title:`) → Save.

**No HACS addons required** — both dashboards use only standard Home Assistant card types
(`entities`, `glance`, `tile`, `button`, `gauge`, `history-graph`, `thermostat`,
`horizontal-stack`, `vertical-stack`, `markdown`).

### `dashboard.yaml` — Unified (Real + Simulation)

| View | Layout | Description |
|---|---|---|
| ☕ Control | 3-column `horizontal-stack` | Controls (left) · Buttons + live sensors (centre) · Simulation physics (right) |
| 📊 Live Data | Single column | Temperature gauge + history-graph charts for temperature, flow, SSR duty, pressure |
| ⚙️ Advanced | Single column | Direct valve/pump control + system diagnostics |

### `dashboard_mock.yaml` — Dedicated Simulation Dashboard

| View | Layout | Description |
|---|---|---|
| 🧪 Control | 3-column `horizontal-stack` | Controls (left) · Buttons + live sensors (centre) · Puck presets + physics parameters (right) |
| 📊 Live Data | Single column | Temperature gauge + history-graph charts for temperature, flow, SSR duty, pressure, nozzle |
| ⚙️ Advanced | Single column | Direct valve/pump control + mock system diagnostics |

> ⚡ **Maintenance rule:** Whenever a new entity is added to the ESPHome firmware, add the
> corresponding card to both `dashboard.yaml` and `dashboard_mock.yaml` in the same PR.
> Real-device cards use the prefix `philips_barista_brew`.
> Mock-device cards use the prefix `philips_barista_brew_mock`.

The legacy per-device dashboards (`espresso_real.yaml`, `espresso_mock.yaml`) are kept for
reference. Use `dashboard.yaml` or `dashboard_mock.yaml` for new installations.

---

Below is a ready-to-paste Lovelace dashboard card configuration for the espresso machine.

---

Below is a ready-to-paste Lovelace dashboard card configuration for the espresso machine.
Add it via **Settings → Dashboards → Edit Dashboard → Add Card → Manual**.

## Entity Category Design Decisions

The entity categories below are intentional and documented here to prevent accidental
reclassification. Each decision is based on the following principles:

| Category | HA Section | When to use |
|---|---|---|
| _(none)_ | Controls | Primary user-facing entities — things users interact with on every shot |
| `entity_category: config` | Configuration | Long-lived tuning parameters and internal hardware parameters that rarely change |
| `entity_category: diagnostic` | Diagnostics | Internal system entities, hardware switches, and mock-simulation-only sensors |

### Rationale for specific entity placements

- **Valves** (`diagnostic`) — solenoid valves are internal hardware; direct control is for
  diagnostics/testing only, not for daily use.
- **Pump** (`diagnostic`) — same rationale as valves; the orchestrator controls the pump, not the user.
- **Climate PID entity** (`diagnostic`) — the raw PID entity is an implementation detail.
  Users control brew temperature through the "Brew Temperature" number entity (Controls).
  Advanced users can still access the PID entity in Diagnostics.
- **Brew Temperature** (Controls) — this is the primary way users set brew temperature.
  No entity_category so it appears prominently at the top of the device page.
- **Machine Mode** (Controls) — primary status indicator; users need to see the current
  machine state at a glance alongside brew controls.
- **Brew Flow Max** (Controls) — adjustable per-shot parameter, not a one-off config value.
  Users may change this between shots so it lives in Controls.
- **Grind Time** (`config`) — a one-off calibration value; not changed per-shot.
- **PID Autotune / Reset Flow / Restart** (Controls) — action buttons; appear as primary
  controls alongside Brew Start/Stop for easy access.
- **Mock physics parameters** (`config`) — simulation tuning knobs; rarely changed once
  calibrated, so they live in Configuration.
- **Mock-only sensors** (nozzle flow, SSR duty, brew pump pressure) (`diagnostic`) —
  these sensors have no equivalent in real hardware and are purely for simulation
  observation / debugging, so they live in Diagnostics.

> **Rule:** Do not move entities out of these categories without updating this document.
> When switching from mock to real hardware, only the `espresso_machine_mock_*` blocks
> (and their associated Config/Diagnostic entities) are removed. All shared entity categories
> must be identical between `philips_barista_brew.yaml` and `philips_barista_brew_mock.yaml`.

## Entity Reference

After flashing, these entities will appear in HA (prefix: `philips_barista_brew`).
Entities are grouped by their `entity_category` on the HA device page:

### Primary entities (no `entity_category` — shown in "Controls" at the top of the device page)

| Domain | Entity | Description |
|---|---|---|
| `switch` | `machine_power` | Master on/off toggle for the espresso machine |
| `text_sensor` | `machine_mode` | Current machine state: `idle` / `brewing` / `steaming` |
| `number` | `brew_temperature` | Brew target temperature (°C) — syncs with PID setpoint |
| `number` | `brew_flow_max` | Stop brew at this volume (mL) — adjustable without reflashing |
| `button` | `brew_start` | Start an espresso shot sequence |
| `button` | `brew_stop` | Abort the current shot immediately |
| `button` | `steam_start` | Start a steam / milk-froth sequence |
| `button` | `steam_stop` | Stop steaming and begin cool-down |
| `button` | `grinder` | Trigger a one-shot timed grind |
| `button` | `pid_autotune` | Run PID autotune — see `docs/pid_tuning.md` |
| `button` | `restart` | Restart the ESP32 |
| `sensor` | `thermoblock_temperature` | Live thermoblock temperature (°C) |
| `sensor` | `brew_flow_rate` | Live flow rate (mL/s) during extraction |
| `sensor` | `brew_flow_total` | Accumulated volume this shot (mL) |
| `sensor` | `last_shot_duration` | Duration of the last completed shot (s) |
| `sensor` | `last_shot_yield` | Net yield of the last shot = volume − puck absorption (mL) |

### Configuration entities (`entity_category: config` — shown under "Configuration")

| Domain | Entity | Description |
|---|---|---|
| `number` | `grind_time` | Adjust grind duration (ms) from HA |

### Diagnostic entities (`entity_category: diagnostic` — shown under "Diagnostics")

These are internal hardware controls and system metrics, separated from primary
user-facing entities to keep the device page uncluttered.

| Domain | Entity | Description |
|---|---|---|
| `climate` | `main_heater` | PID heater — internal controller; use "Brew Temperature" for daily use |
| `switch` | `brew_valve` | Brew solenoid valve (direct hardware control) |
| `switch` | `steam_valve` | Steam solenoid valve (direct hardware control) |
| `switch` | `purge_valve` | Purge / drain solenoid valve (direct hardware control) |
| `switch` | `vibration_pump` | Pump relay (direct hardware control) |
| `sensor` | `uptime` | Device uptime |
| `sensor` | `wi_fi_signal` | Wi-Fi signal strength (dBm) |
| `sensor` | `esp32_temperature` | Internal ESP32 chip temperature |

> **Tip — setting temperature above 90 °C:** Use the `Brew Temperature` number entity.
> For finer PID control (e.g. checking integral/derivative live), use the
> `climate.main_heater` entity card in the Diagnostics section.

---

## Mock Device Entity Categories (`philips_barista_brew_mock`)

The mock device adds simulation-specific entities on top of the real device entities.
These are categorised as follows on the HA device page:

> **Note:** All mock-specific entities live in Config or Diagnostics. When switching to
> real hardware, removing the `espresso_machine_mock_heater` and
> `espresso_machine_mock_pump` YAML blocks removes all mock categories automatically.
> No shared entity categories change between mock and real configs.

### Mock Configuration entities (`entity_category: config` — shown under "Configuration")

| Domain | Entity | Description |
|---|---|---|
| `number` | `mock_heater_power` | Heater element power (W) |
| `number` | `mock_thermal_mass` | Thermal mass of the thermoblock (J/°C) |
| `number` | `mock_heat_loss` | Heat loss rate (W/°C) |
| `number` | `mock_ambient_temperature` | Ambient room temperature (°C) |
| `number` | `mock_nominal_flow` | Pump nominal flow rate (mL/s) |
| `number` | `mock_pump_max_pressure` | Pump stall pressure (bar) |
| `number` | `mock_puck_time_constant` | Puck wetting time constant (s) |
| `number` | `mock_puck_density` | Puck resistance (1 = open, 100 = blocked) |
| `number` | `mock_internal_volume` | Internal tubing volume (mL) — residual flow decay |
| `button` | `puck_open_d_1` | Set puck density to 1 (fully open) |
| `button` | `puck_soft_d_25` | Set puck density to 25 (soft puck) |
| `button` | `puck_medium_d_50` | Set puck density to 50 (medium puck) |
| `button` | `puck_hard_d_75` | Set puck density to 75 (hard puck) |
| `button` | `puck_blocked_d_100` | Set puck density to 100 (blocked) |

### Mock Diagnostic entities (`entity_category: diagnostic` — shown under "Diagnostics")

These sensors have no equivalent in real hardware — they are simulation-only observation
points for verifying mock physics behaviour.

| Domain | Entity | Description |
|---|---|---|
| `switch` | `mock_pump` | Mock pump switch (simulation only — real pump is in Diagnostics as `vibration_pump`) |
| `sensor` | `heater_ssr_duty` | Simulated SSR duty cycle (0–100%) |
| `sensor` | `nozzle_flow_rate` | Estimated flow out of the group head nozzle (mL/s) |
| `sensor` | `nozzle_flow_total` | Estimated total flow out of the group head nozzle (mL) |
| `sensor` | `mock_brew_pump_pressure` | Simulated system pressure (bar) — no real hardware equivalent |
| `switch` | `brew_valve` | Brew solenoid valve (direct hardware control) |
| `switch` | `steam_valve` | Steam solenoid valve (direct hardware control) |
| `switch` | `purge_valve` | Purge / drain solenoid valve (direct hardware control) |

```yaml
type: entities
title: ☕ Espresso Machine
entities:
  - entity: text_sensor.philips_barista_brew_machine_mode
    name: Machine State
  - entity: number.philips_barista_brew_brew_temperature
    name: Brew Temperature (°C)
  - entity: number.philips_barista_brew_brew_flow_max
    name: Flow Max (mL)
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
