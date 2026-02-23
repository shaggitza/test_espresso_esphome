# Custom Home Assistant UI — Espresso Machine

This document explains how to install and use the two ready-made Home Assistant
Lovelace dashboards included in this repository, as well as the AppDaemon-powered
**per-shot history graphs** that let you scroll through every espresso shot you have
ever pulled and inspect its temperature curve and flow profile.

> **No SSH required.** The preferred installation path below uses only the
> Home Assistant web UI and two add-ons that are available directly from the
> HA Add-on Store.  SSH is never needed.

---

## Overview

Two dashboard files are provided:

| File | Target | Description |
|---|---|---|
| `home_assistant/dashboards/espresso_real.yaml` | Real hardware (`philips_barista_brew.yaml`) | Full control, live graphs, shot history |
| `home_assistant/dashboards/espresso_mock.yaml` | Mock simulation (`philips_barista_brew_mock.yaml`) | Same as real + Physics Tuning view |

Both dashboards share the same layout and the same shot history feature.  
The mock dashboard adds an extra **Physics Tuning** view that exposes all
simulation parameters (heater power, thermal mass, puck pressure, etc.) so you
can explore how the PID and flow model behave without touching real hardware.

---

## Quick Start — No SSH Required

Everything below can be done entirely through the Home Assistant browser UI.
You will need two add-ons from the HA **Add-on Store** — both are free and
official:

| Add-on | Why you need it |
|---|---|
| **File Editor** | Browser-based file manager/editor for your HA config folder |
| **AppDaemon 4** | Runs the shot-history Python app that records every espresso shot |

Both are installed via **Settings → Add-ons → Add-on Store**.

---

### Step 1 — Install HACS custom cards

Install the following cards via **HACS → Frontend**
(`Settings → HACS → Frontend → Explore & Download Repositories`):

| Card | Repository | Purpose |
|---|---|---|
| **Mushroom** | `piitaya/lovelace-mushroom` | Modern Material Design 3 entity cards |
| **Mini Graph Card** | `kalkih/mini-graph-card` | Compact real-time sparkline graphs |
| **ApexCharts Card** | `RomRider/apexcharts-card` | Full-featured shot history graphs |

After installing each card click **Reload** in HACS, then hard-refresh your
browser (`Ctrl+Shift+R` / `Cmd+Shift+R`).

---

### Step 2 — Install the File Editor add-on

1. **Settings → Add-ons → Add-on Store → search "File Editor"**
2. Install **File Editor**, enable **Show in sidebar**, click **Start**.

The File Editor icon now appears in the left sidebar.  All subsequent file
operations use this editor — no SSH, no terminal.

---

### Step 3 — Add the HA package file (helpers + template sensors)

A single combined file `home_assistant/espresso_package.yaml` ships in this
repository and contains all helpers and template sensors in one place.

**3a — Open the File Editor** and navigate to your HA config root (the folder
that contains `configuration.yaml`).

**3b — Create a new file** called `espresso_package.yaml`.  Paste the entire
contents of
[`home_assistant/espresso_package.yaml`](home_assistant/espresso_package.yaml)
from this repository into it and save.

**3c — Edit `configuration.yaml`** (also via File Editor) and add the
`packages:` block.  YAML is whitespace-sensitive, so use the same indentation
style as the rest of your file (2 spaces shown here):

```yaml
homeassistant:
  packages:
    espresso: !include espresso_package.yaml
```

**3d — Restart Home Assistant** (`Developer Tools → YAML → Restart`).

After the restart you should see four new helper entities under
**Settings → Devices & Services → Helpers**:

- `input_text.espresso_shot_log`
- `input_select.espresso_shot_viewer`
- `input_datetime.espresso_shot_view_start`
- `input_datetime.espresso_shot_view_end`

---

### Step 4 — Install AppDaemon and the shot-history app

**4a — Install AppDaemon:**

1. **Settings → Add-ons → Add-on Store → search "AppDaemon"**
2. Install **AppDaemon 4**, enable **Auto-start** and **Watchdog**, click **Start**.

**4b — Copy the app files using the File Editor:**

The AppDaemon `apps/` directory is located at
`/addon_configs/a0d7b954_appdaemon/apps/` inside the File Editor.

1. Open the File Editor, navigate to
   `/addon_configs/a0d7b954_appdaemon/apps/`.
2. Create a new file called `shot_history.py` and paste the full contents of
   [`home_assistant/appdaemon/shot_history.py`](home_assistant/appdaemon/shot_history.py)
   from this repository.  Save.
3. Create (or open) `apps.yaml` in the same folder and add the following block
   (choose **real** or **mock** depending on your setup):

   **Real hardware:**
   ```yaml
   espresso_shot_history:
     module: shot_history
     class: ShotHistory
     mode_entity: text_sensor.philips_barista_brew_machine_mode
     duration_entity: sensor.philips_barista_brew_last_shot_duration
     yield_entity: sensor.philips_barista_brew_last_shot_yield
     temp_entity: sensor.philips_barista_brew_thermoblock_temperature
     max_shots: 20
     graph_padding_sec: 15
   ```

   **Mock simulation:**
   ```yaml
   espresso_shot_history:
     module: shot_history
     class: ShotHistory
     mode_entity: text_sensor.philips_barista_brew_mock_machine_mode
     duration_entity: sensor.philips_barista_brew_mock_last_shot_duration
     yield_entity: sensor.philips_barista_brew_mock_last_shot_yield
     temp_entity: sensor.philips_barista_brew_mock_thermoblock_temperature
     max_shots: 20
     graph_padding_sec: 15
   ```

4. **Restart AppDaemon** (Settings → Add-ons → AppDaemon → Restart).

---

### Step 5 — Add the dashboard

1. In HA go to **Settings → Dashboards → Add Dashboard**
2. Choose "Empty dashboard", name it **☕ Espresso Machine** (or **☕ Espresso Sim**)
3. Open the new dashboard, click the ✏️ **Edit Dashboard** icon (top right)
4. Click the **⋮ three-dot menu → Raw configuration editor**
5. Select all, delete, then paste the YAML from the appropriate file:
   - Real hardware → [`home_assistant/dashboards/espresso_real.yaml`](home_assistant/dashboards/espresso_real.yaml)
   - Mock simulation → [`home_assistant/dashboards/espresso_mock.yaml`](home_assistant/dashboards/espresso_mock.yaml)
6. Click **Save**

That's it — pull an espresso shot and it will appear automatically in the
**Shot History** view.

---

## Alternative: File-based Dashboard (version-controlled)

If you prefer to keep your dashboard in a YAML file (so it is tracked by git
or other tools), use the File Editor to copy the dashboard YAML into your HA
config directory, then add to `configuration.yaml`:

```yaml
lovelace:
  dashboards:
    espresso-machine:
      mode: yaml
      filename: espresso_dashboard.yaml
      title: "☕ Espresso Machine"
      icon: mdi:coffee-maker
      show_in_sidebar: true
```

Restart HA after saving.

---

## Alternative: Manual File-based Installation (helpers split across two files)

If you already have a split `configuration.yaml` setup and prefer keeping
helpers and template sensors in separate files, you can use the original
individual files instead of the combined package:

```yaml
homeassistant:
  packages:
    espresso_helpers:   !include helpers.yaml
    espresso_templates: !include template_sensors.yaml
```

Copy `home_assistant/helpers.yaml` and `home_assistant/template_sensors.yaml`
to your HA config directory via the File Editor, then add the lines above to
`configuration.yaml` and restart HA.

---

## Dashboard Views

### Real Hardware Dashboard (`espresso_real.yaml`)

#### View 1 — Control

| Card | Description |
|---|---|
| Status chips | Machine mode (IDLE / BREWING / STEAMING) + live temperature + live volume |
| Heater climate card | Set brew/steam temperature; shows current vs target |
| Temperature graph | 30-minute live sparkline — colour-coded by temperature |
| Brew / Steam buttons | Four action buttons: Brew Start, Brew Stop, Steam Start, Steam Stop |
| Grinder controls | One-shot grind button + grind time number slider |
| Live flow graph | Real-time flow rate (mL/s) and accumulated volume (mL) |
| Last Shot glance | Duration, yield, and temperature of the most recent completed shot |

#### View 2 — Shot History

| Card | Description |
|---|---|
| Shot count chip | How many shots are in the rolling log |
| Shot log table | Scrollable Markdown table — all recorded shots with date, duration, yield, temperature |
| Shot selector | Dropdown to pick any recorded shot for graph inspection |
| Temperature graph | ApexCharts card showing the thermoblock temperature during the selected shot |
| Flow rate graph | ApexCharts card showing flow rate + volume during the selected shot |
| Context graph | History-graph showing the last 2 hours of temperature (context view) |

#### View 3 — Advanced

| Card | Description |
|---|---|
| Valve switches | Direct open/close control of all three solenoid valves |
| Pump switch | Direct relay control of the vibration pump |
| Diagnostics | Uptime, Wi-Fi signal, ESP32 chip temperature |
| Maintenance buttons | PID Autotune trigger, ESP32 Restart |
| 24-hour temperature graph | Full-day temperature history |

---

### Mock Dashboard (`espresso_mock.yaml`)

Includes all views from the real dashboard, plus:

#### View 3 — Physics Tuning (mock-only)

| Parameter | Entity | Default | Effect |
|---|---|---|---|
| Heater Power | `number.…_mock_heater_power` | 1200 W | Heating rate — higher = faster heat-up |
| Thermal Mass | `number.…_mock_thermal_mass` | 800 J/°C | Heat capacity — higher = slower response |
| Heat Loss | `number.…_mock_heat_loss` | 1.7 W/°C | Ambient cooling — higher = faster cool-down |
| Ambient Temp | `number.…_mock_ambient_temperature` | 25 °C | Environment temperature |
| Nominal Flow | `number.…_mock_nominal_flow` | 4.0 mL/s | Pump flow at 9 bar rated pressure |
| Pump Max Pressure | `number.…_mock_pump_max_pressure` | 15 bar | Pump stall pressure |
| Puck Time Constant | `number.…_mock_puck_time_constant` | 10 s | Puck wetting speed |
| Puck Pressure | `number.…_mock_puck_pressure` | 9 bar | Coffee puck resistance |

**Puck presets:**

| Button | Puck Pressure | Expected Flow | Expected τ_eff |
|---|---|---|---|
| Easy (6 bar) | 6 bar | ~6.0 mL/s | ~6.7 s |
| Nominal (9 bar) | 9 bar | ~4.0 mL/s | ~10 s |
| Hard (12 bar) | 12 bar | ~2.0 mL/s | ~13.3 s |

---

## Shot History Feature — How It Works

```
┌───────────────────────────────────────────────────────────────────────────┐
│  ESPHome device                 HA                      Dashboard         │
│                                                                           │
│  machine_mode: brewing  ──→  AppDaemon records         Shot log table    │
│  machine_mode: idle     ──→  shot to JSON log    ──→   updates instantly  │
│                              updates input_select                         │
│                                                                           │
│  User picks shot from                                                     │
│  input_select           ──→  AppDaemon sets            ApexCharts cards  │
│                              input_datetime helpers ──→ snap to shot      │
│                              (start + end)             time window        │
└───────────────────────────────────────────────────────────────────────────┘
```

1. **AppDaemon** (`shot_history.py`) watches `text_sensor.…_machine_mode`.
2. When the mode transitions `brewing → idle`, the app records:
   - Shot number (sequential)
   - Start and end times (UTC ISO 8601)
   - Duration (from the ESPHome `last_shot_duration` sensor)
   - Yield in mL (from `last_shot_yield`)
   - Temperature at shot end
3. The shot log (JSON array) is stored in `input_text.espresso_shot_log`.
4. `input_select.espresso_shot_viewer` is refreshed with human-readable labels
   and auto-selects the newest shot.
5. On selection, AppDaemon sets `input_datetime.espresso_shot_view_start` and
   `input_datetime.espresso_shot_view_end` to the shot's time window
   (padded by `graph_padding_sec` seconds on each side).
6. Template sensors (`sensor.espresso_selected_shot_span_sec` and
   `sensor.espresso_selected_shot_end_offset`) compute the ApexCharts
   `graph_span` and `span.end` values.
7. The two ApexCharts cards on the Shot History view automatically update.

**Up to 20 shots** are kept in the rolling log.  
The log persists across HA restarts via the `input_text` helper.

---

## Customisation

### Changing the entity prefix

If your ESPHome device uses a different name than `philips_barista_brew`,
do a search-and-replace in the dashboard YAML:

```bash
# Example: rename to my_espresso
sed -i 's/philips_barista_brew/my_espresso/g' espresso_real.yaml
```

### Showing both real and mock entities simultaneously

You can run both the real and mock device at the same time (e.g. for
side-by-side PID tuning). Simply add both dashboards and configure
a second AppDaemon app instance in `apps.yaml` with a unique key name:

```yaml
espresso_shot_history_mock:
  module: shot_history
  class: ShotHistory
  mode_entity: text_sensor.philips_barista_brew_mock_machine_mode
  ...
```

### Adjusting the shot graph window

Edit `graph_padding_sec` in `apps.yaml` to add more or less padding around
each shot in the history graphs.  Setting it to `0` shows only the exact
shot window; `30` gives a 30-second buffer on each side.

### Keeping more shots in history

Set `max_shots: 50` in `apps.yaml` to keep the last 50 shots.  
Note that `input_text` helpers are limited to 10 000 characters — at ~500
chars per shot this allows approximately 20 shots before truncation.
If you need more, consider using a `notify.file` action to write shots to
a CSV and only keep the last 10 in the HA helper for the dashboard.

---

## Troubleshooting

### Cards show "Custom element doesn't exist"

One or more HACS custom cards are missing.  
Check HACS → Frontend and verify all three cards are installed and loaded.

### Shot history is empty / not updating

1. Check the AppDaemon log in HA (`Settings → Add-ons → AppDaemon → Log`)
   for `ShotHistory` messages.
2. Verify the `mode_entity` in `apps.yaml` matches your device's actual entity ID.
   In HA, go to `Developer Tools → States` and search for your device's machine mode entity.
3. Ensure `input_text.espresso_shot_log` and `input_select.espresso_shot_viewer` exist
   (create them by restarting HA after adding `helpers.yaml`).

### ApexCharts shows "No data" for the selected shot

1. Verify `sensor.espresso_selected_shot_end_offset` is returning a value like `-300sec`.
2. Check that HA `recorder` is enabled and retaining enough history days
   (`recorder: purge_keep_days: 30` in `configuration.yaml`).
3. The shot history graph relies on the HA database — if the shot happened more than
   `purge_keep_days` ago, the raw sensor data will have been pruned.

### "Brew Start" button does nothing

Confirm the button entity ID matches your device.  
Use `Developer Tools → States` to search for `button.*brew_start`.

---

## Entity Quick-Reference

### Real hardware (`philips_barista_brew` prefix)

| Domain | Entity ID suffix | Description |
|---|---|---|
| `climate` | `main_heater` | PID heater — set temperature |
| `text_sensor` | `machine_mode` | idle / brewing / steaming |
| `button` | `brew_start` | Start an espresso shot |
| `button` | `brew_stop` | Abort the current shot |
| `button` | `steam_start` | Start steaming |
| `button` | `steam_stop` | Stop steaming |
| `button` | `grinder` | One-shot timed grind |
| `number` | `grind_time` | Grind duration (ms) |
| `sensor` | `thermoblock_temperature` | Live temperature (°C) |
| `sensor` | `brew_flow_rate` | Live flow rate (mL/s) |
| `sensor` | `brew_flow_total` | Accumulated shot volume (mL) |
| `sensor` | `last_shot_duration` | Duration of last shot (s) |
| `sensor` | `last_shot_yield` | Net yield of last shot (mL) |
| `switch` | `brew_valve` | Brew solenoid valve |
| `switch` | `steam_valve` | Steam solenoid valve |
| `switch` | `purge_valve` | Purge / drain valve |
| `switch` | `vibration_pump` | Pump relay |

### Mock simulation (`philips_barista_brew_mock` prefix, additional entities)

| Domain | Entity ID suffix | Description |
|---|---|---|
| `sensor` | `heater_ssr_duty` | SSR duty cycle (0–100 %) |
| `number` | `mock_heater_power` | Simulated heater power (W) |
| `number` | `mock_thermal_mass` | Simulated thermal mass (J/°C) |
| `number` | `mock_heat_loss` | Simulated heat loss (W/°C) |
| `number` | `mock_ambient_temperature` | Simulated ambient temp (°C) |
| `number` | `mock_nominal_flow` | Nominal pump flow (mL/s) |
| `number` | `mock_pump_max_pressure` | Pump stall pressure (bar) |
| `number` | `mock_puck_time_constant` | Puck wetting τ (s) |
| `number` | `mock_puck_pressure` | Puck resistance (bar) |
| `button` | `puck_easy_6_bar` | Set puck pressure to 6 bar |
| `button` | `puck_nominal_9_bar` | Set puck pressure to 9 bar |
| `button` | `puck_hard_12_bar` | Set puck pressure to 12 bar |
| `button` | `reset_flow` | Reset the flow accumulator |

---

## File Structure

```
home_assistant/
├── README.md              → You are here
├── helpers.yaml           → HA input helpers (add to configuration.yaml)
├── template_sensors.yaml  → Template sensors for shot viewer (add to configuration.yaml)
├── dashboards/
│   ├── espresso_real.yaml → Lovelace dashboard — real hardware
│   └── espresso_mock.yaml → Lovelace dashboard — mock simulation
└── appdaemon/
    ├── apps.yaml          → AppDaemon app configuration
    └── shot_history.py    → Shot history tracker AppDaemon app
```
