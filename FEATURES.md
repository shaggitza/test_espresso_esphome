# Feature Status — ESPHome Espresso Machine Controller

This file is the **authoritative record** of what is implemented, partially implemented, and
planned in this project.

> **Developer / Agent instruction:** Whenever you implement, fix, or stub out a feature,
> update this file immediately — in the same PR as your code change. Keep it accurate.
> No feature may be described as implemented here unless it is fully functional and covered
> by tests.

---

## Status Legend

| Symbol | Meaning |
|--------|---------|
| ✅ | Fully implemented — schema, C++ runtime, and tests all present |
| 🚧 | Partially implemented — schema and/or stub C++ exists; not fully functional |
| ⬜ | Planned — designed but no code written yet |

---

## Platform Components

| Platform | ESPHome entity type | Status | Notes |
|---|---|---|---|
| `espresso_machine_flow_meter` | `sensor` (rate + total + 3 s avg) | ✅ | ISR-driven pulse counter; `reset` and `calibrate` actions |
| `espresso_machine_valve` | `switch` | ✅ | Single-open interlock enforced at platform level; `open`/`close` actions |
| `espresso_machine_pump` (relay) | `switch` | ✅ | On/off relay; `run` action with volume + timeout |
| `espresso_machine_pump` (dimmer) | `number` (0–100 %) | ✅ | Slow-PWM dimmer stub; `turn_on`/`turn_off` wired |
| `espresso_machine_grinder` | `button` + `number` | ✅ | Timed relay grind; adjustable grind-time number entity |
| `espresso_machine` (orchestrator) | `component` | ✅ | Brew + steam state machines; purge-before-steam; steam timeout; temperature management via `IHeater`; all P0/P1/P2 items complete |
| `espresso_machine_profile` | `select` + config | ⬜ | Planned (Phase 12); see `docs/profiles.md` |
| `espresso_machine_sprofiler` | `component` | 🚧 | **BETA** — Sprofiler cloud shot upload; not fully verified yet; opt-in only (not included in default examples); accepts `espresso_machine:` reference for auto-recording during brew; Gaggiuino-compatible JSON serialisation; configurable server URL and Bearer token auth; virtual `http_post()` for test mocking; see `docs/sprofiler.md` |
| `espresso_machine_mock_heater` | `output` + `sensor` | ✅ | Thermal ODE simulation; HA-tunable physics parameters; optional 3-node thermal distance model (dist_water_to_heater_mm, dist_water_to_sensor_mm, dist_sensor_to_heater_mm) to simulate Al block lag and make PID harder |
| `espresso_machine_mock_pump` | `switch` | ✅ | Puck wetting flow model; HA-tunable physics parameters |
| `espresso_machine_mock_scale` | `sensor` (weight + flow) | ⬜ | Planned (Phase 13b); cup mode: derives weight from mock pump nozzle output; portafilter mode: accumulates at `dose_rate_g_per_s`; auto-tare on brew/grind start; see `docs/scales.md` |
| `espresso_machine_heater` | `component` | ✅ | Production `IHeater` adapter wrapping `climate::Climate`; implements `get_current_temperature()`, `set_target_temperature()`, `force_off()`; optional `ssr_output` + `ssr_period_number` to expose SSR switching period (ms) as HA number entity for runtime tuning |

---

## Orchestrator Features

### Brew Mode

| Feature | Status | Notes |
|---|---|---|
| Brew state machine (`IDLE→[COOLING→]HEATING→[PRE_INFUSION→]BREWING→DONE→CLEANUP`) | ✅ | All states; `HEATING` gates on temperature when `brew_heater_ctrl` is wired (P1-2); optional pre-brew `COOLING` state when `temperature_cooldown: true` and thermoblock is above target |
| Heater setpoint wiring (climate call) | ✅ | `set_target_temperature(brew_target_temp_)` called via `IHeater` in `brew_start()`; `espresso_machine_heater` adapter wires production `climate.pid` (P1-2) |
| Heater readiness tolerance (`temperature_tolerance`) | ✅ | `EspressoMachineHeater` overrides `IHeater::is_ready()` to treat temperatures within `temperature_tolerance` (default 0.5°C) of the target as ready; prevents indefinite wait when PID stabilises just below setpoint (e.g. 89.9°C at 90.0°C target) |
| Temperature surfing (offset + ramp) | ✅ | Config accepted; ramp setpoint computed and applied to `brew_heater_ctrl_->set_target_temperature()` each BREWING tick; runtime HA switch (`temp_surf_switch`) enables/disables surfing without reflashing (P1-3) |
| Brew temperature cooldown (`temperature_cooldown`) | ✅ | When `true`, if the thermoblock is above `target_temperature` when `brew_start()` is called (e.g. after an aborted steam session), the brew sequence inserts a `COOLING` state **before** `HEATING`: purge valve opens, pump runs in bypass mode, heater setpoint lowered to `target_temperature`; waits for thermoblock to drop to target then closes purge and transitions to `HEATING` as normal. Requires `heater_controller:` to be wired; ignored otherwise |
| Pre-infusion (low-pressure pre-wet) | ✅ | Volume-driven flowing phase + hold timer; resets flow before main extraction |
| Auto-terminate at `flow_max` | ✅ | Shot stops when flow meter reports ≥ `flow_max` ml |
| Shot stats (`last_shot_time_s`, `last_shot_volume_ml`) | ✅ | Stored on the orchestrator |
| Shot stats as HA sensor entities | ✅ | `shot_stats.last_shot_time`, `last_shot_volume`, `last_shot_yield` in brew schema; published at BREWING→DONE (P1-5) |
| Cleanup script callback after brew | ✅ | `set_brew_cleanup_fn()` called in DONE state; wired from YAML via `cleanup_script:` (P2-1) |
| `brew_start` / `brew_stop` actions | ✅ | Available from YAML automations and HA services |

### Steam Mode

| Feature | Status | Notes |
|---|---|---|
| Steam state machine (`IDLE→HEATING→PURGING→STEAMING→COOLING→CLEANUP`) | ✅ | All states; `PURGING` flushes residual water before steam; `HEATING` gates on temperature when `heater_controller` is wired |
| Heater setpoint to steam temperature | ✅ | `set_target_temperature(steam_target_temp_)` called on `IHeater` in `steam_start()` |
| Temperature-gated HEATING→PURGING/STEAMING transition | ✅ | Waits for `heater.is_ready(steam_target_temp_)`; heater reports ready within `temperature_tolerance` (default 0.5°C) of the target; falls back to immediate if no IHeater wired |
| Purge-before-steam (`purge_volume`) | ✅ | Pumps configured volume through purge valve to clear residual water; `purge_volume: 0` (default) skips phase (backward-compatible) |
| Steam valve + pump activation | ✅ | Steam valve opens and pump starts in STEAMING state (after purge, if configured) |
| Pump duty-cycle flow-rate control (delegated to pump) | ✅ | Orchestrator calls `pump->set_target_flow(steam_flow_max_ml_per_s_)` when entering STEAMING; pump's `loop()` does bang-bang on/off to maintain the rate, honouring `pump_min_on_time` / `pump_min_off_time` |
| Steam pump minimum on-window (`pump_min_on_time`) | ✅ | Prevents rapid pump cycling; default 500 ms; configurable via `pump_min_on_time:` in the `espresso_machine_pump:` component block (P2-7) |
| Steam safety timeout (`timeout`) | ✅ | Optional auto-stop after configured duration; 0 = disabled (default) |
| Purge on steam stop | ✅ | Purge valve opens immediately when steam stops to flush steam path during cool-down |
| Auto cool-down after steaming | ✅ | `set_target_temperature(steam_cool_down_to_)` on `IHeater`; temperature-gated COOLING→CLEANUP transition |
| Temperature-gated COOLING→CLEANUP transition | ✅ | Waits for `get_current_temperature() <= steam_cool_down_to_`; falls back to immediate if no IHeater wired |
| `IHeater` interface | ✅ | `get_current_temperature()` + `set_target_temperature()` + `is_ready(target)` in `interfaces.h`; default `is_ready()` uses exact `>=`; `EspressoMachineHeater` overrides with configurable tolerance |
| `heater_controller:` YAML key | ✅ | Optional in steam schema; wires an `IHeater*` to the orchestrator |
| Cleanup script callback after steam | ✅ | `set_steam_cleanup_fn()` called in CLEANUP state; wired from YAML via `cleanup_script:` (P2-1) |
| `steam_start` / `steam_stop` actions | ✅ | Available from YAML automations and HA services |

### Safety Interlocks

| Feature | Status | Notes |
|---|---|---|
| Valve single-open interlock | ✅ | Platform-level; opening any valve closes all others automatically |
| Brew/steam mutual exclusion | ✅ | Orchestrator rejects `brew_start` during steam and vice versa |
| Grinder independence | ✅ | **Design decision:** Grinder and brew/steam are independent operations; no lockout needed |
| Hard over-temperature cutoff | ✅ | `check_over_temp_safety_()` in orchestrator loop; latching cutoff flag; tested via `Safety_OverTempCutoff` |
| Sensor NaN detection | ✅ | NaN temperature triggers `IHeater::force_off()`; tested via `Safety_SensorNaNForcesHeaterOff` |
| Brew timeout (Wi-Fi disconnect safety) | ✅ | `set_brew_timeout_ms()` config; auto-stops brew if flow sensor or Wi-Fi fails |
| Watchdog (heater-off on reset) | ✅ | Native ESPHome watchdog; SSR GPIO defaults LOW on reset |
| `safe_stop_all()` on brew/steam stop | ✅ | Closes all valves, stops all pumps immediately |

### Maintenance

| Feature | Status | Notes |
|---|---|---|
| `espresso_machine.flush` action | ✅ | Pumps N ml through brew purge valve; only accepted when idle (P2-2) |
| `espresso_machine.descale_start` action | ✅ | Automated descale cycle: N pump-on/soak iterations through the brew purge valve; configurable `cycles`, `pump_time`, `soak_time`; stops early with `descale_stop()`; uses brew hardware (no additional hardware required) |
| `espresso_machine.backflush_start` action | ✅ | Automated backflush cycle: N pressurize/release iterations through the brew valve (3-way solenoid required); configurable `cycles`, `pressurize_time`, `release_time`; stops early with `backflush_stop()`; uses brew hardware (blind filter required) |
| Status text sensor (`status_sensor`) | ✅ | Optional `text_sensor` entity in `espresso_machine:` block; publishes a verbose, live status string to HA on every state transition and on every `loop()` tick (deduplicated); includes real-time values such as current/target temperature and flow volume, e.g. `"Heating to 90.0°C (now 85.3°C)"`, `"Brewing: 15.2 ml / 40.0 ml"`, `"Cooling to 90.0°C (now 125.3°C)"`, `"Flushing: 12.3 ml / 50.0 ml"`, `"Descaling: pumping (cycle 1/3)"`, `"Backflush: pressurizing (cycle 2/5)"` |
| Idle auto-off (`idle_timeout`) | ✅ | Automatically calls `machine_off()` when the machine has been powered on but idle for `idle_timeout` (default 30 min). Set to `0` to disable. Config-only — not adjustable at runtime from HA. Idle timer resets on `machine_on()` and on every transition back to IDLE (after brew, steam, or flush completes). |

---

## Native ESPHome Features Used

These are **not** custom platforms — they are standard ESPHome entities used by the reference
configuration and documented in the example YAML.

| Feature | Status | Notes |
|---|---|---|
| PID temperature control (`climate.pid`) | ✅ | Full ESPHome PID with tunable kp/ki/kd and autotune button |
| Thermocouple support (MAX6675 / MAX31855) | ✅ | Both options documented in example; NTC also supported |
| SSR output (`output.slow_pwm`) | ✅ | 1 s period for SSR duty-cycle control; period adjustable from HA via `ssr_period_number` on the `espresso_machine_heater` entity |
| Home Assistant native API | ✅ | All entities auto-discovered; API encryption supported |
| OTA updates | ✅ | Standard ESPHome OTA via Wi-Fi |
| OLED display (SSD1306) | ✅ | Optional; display lambda documented in example YAML |
| PID autotune button | ✅ | Template button triggers `climate.pid.autotune` |
| HA dashboard — unified (`dashboard.yaml`) | ✅ | 3-column Control view (controls · buttons+sensors · simulation) + Live Data graphs + Advanced; no HACS required; paste into HA raw config editor; see `home_assistant/dashboards/dashboard.yaml` |
| HA dashboard — simulation (`dashboard_mock.yaml`) | ✅ | Dedicated mock dashboard: 3-column Control (controls · buttons+sensors · puck presets+physics) + Live Data graphs + Advanced; no HACS required; see `home_assistant/dashboards/dashboard_mock.yaml` |
| MCU metrics (`debug` component) | ✅ | Free heap memory (`Heap Free`) and main-loop time (`Loop Time`) exposed as diagnostic sensor entities in HA; `debug: update_interval: 30s` in both example configs |

---

## Planned Features (No Code Yet)

| Feature | Target Phase | Notes |
|---|---|---|
| Brew profiles (`espresso_machine_profile:`) | Phase 12 | Multi-phase pressure/flow curves; runtime HA select |
| Gaggiuino/GaggiaMate profile import | Phase 12 | Python CLI converter planned |
| **Scale platform — Bluetooth** (`espresso_machine_scale`, `type: bluetooth`) | Phase 13 | ESP32 BLE connection to Acaia Lunar/Pearl, Bookoo, Felicita Arc, Difluid; weight-based brew exit + grinder dose exit; see docs/scales.md |
| **Scale platform — Wired load cell** (`espresso_machine_scale`, `type: load_cell`) | Phase 13 | HX711 or NAU7802 ADC; tare, calibrate actions; same `IScale` interface as BT variant; see docs/scales.md |
| Weight-based brew exit (`target_weight:` in brew) | Phase 13 | Scale takes priority over flow meter; falls back to volume on stale/disconnect |
| Weight-based grinder dose exit (`target_dose:` on grinder) | Phase 13 | Stops grind when portafilter scale reaches target; falls back to `default_grind_time` |
| Shot statistics extended (weight + brew ratio) | Phase 13 | `last_shot_weight_g`, `last_shot_brew_ratio` HA sensor entities |
| Pressure transducer | Future | Requires ADC + transducer hardware |
