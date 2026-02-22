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
| `espresso_machine_flow_meter` | `sensor` (rate + total) | ✅ | ISR-driven pulse counter; `reset` and `calibrate` actions |
| `espresso_machine_valve` | `switch` | ✅ | Single-open interlock enforced at platform level; `open`/`close` actions |
| `espresso_machine_pump` (relay) | `switch` | ✅ | On/off relay; `run` action with volume + timeout |
| `espresso_machine_pump` (dimmer) | `number` (0–100 %) | ✅ | Slow-PWM dimmer stub; `turn_on`/`turn_off` wired |
| `espresso_machine_grinder` | `button` + `number` | ✅ | Timed relay grind; adjustable grind-time number entity |
| `espresso_machine` (orchestrator) | `component` | 🚧 | Brew + steam state machines present; see brew/steam rows below |
| `espresso_machine_profile` | `select` + config | ⬜ | Planned (Phase 12); see `docs/profiles.md` |

---

## Orchestrator Features

### Brew Mode

| Feature | Status | Notes |
|---|---|---|
| Brew state machine (`IDLE→HEATING→BREWING→DONE→CLEANUP`) | 🚧 | All states exist; `HEATING` transitions immediately (no live temperature check yet) |
| Heater setpoint wiring (climate call) | 🚧 | Heater stored as `Component*`; `set_target_temperature()` call not yet wired (Phase 2) |
| Temperature surfing (offset + ramp) | 🚧 | Config accepted; ramp value computed but NOT applied to the climate entity yet |
| Pre-infusion (low-pressure pre-wet) | ✅ | Volume-driven flowing phase + hold timer; resets flow before main extraction |
| Auto-terminate at `flow_max` | ✅ | Shot stops when flow meter reports ≥ `flow_max` ml |
| Shot stats (`last_shot_time_s`, `last_shot_volume_ml`) | ✅ | Stored on the orchestrator; not yet exposed as HA sensor entities |
| Shot stats as HA sensor entities | ⬜ | Planned (Phase 7 completion) |
| Cleanup script after brew | ⬜ | Schema accepts block; not executed (Phase 9) |
| `brew_start` / `brew_stop` actions | ✅ | Available from YAML automations and HA services |

### Steam Mode

| Feature | Status | Notes |
|---|---|---|
| Steam state machine (`IDLE→HEATING→STEAMING→COOLING→CLEANUP`) | 🚧 | All states exist; `HEATING` transitions immediately (no live temperature check) |
| Heater setpoint to steam temperature | 🚧 | Not wired — heater climate call pending Phase 2 |
| Steam valve + pump activation | ✅ | `steam_start()` opens the valve and starts the pump in STEAMING state |
| Pump duty-cycle flow-rate control | ⬜ | Planned (Phase 8); pump runs continuously for now |
| Auto cool-down after steaming | 🚧 | `cool_down_to` config accepted; heater setpoint NOT reset (Phase 2 wiring pending) |
| Cleanup script after steam | ⬜ | Schema accepts block; not executed (Phase 9) |
| `steam_start` / `steam_stop` actions | ✅ | Available from YAML automations and HA services |

### Safety Interlocks

| Feature | Status | Notes |
|---|---|---|
| Valve single-open interlock | ✅ | Platform-level; opening any valve closes all others automatically |
| Brew/steam mutual exclusion | ✅ | Orchestrator rejects `brew_start` during steam and vice versa |
| Grinder lockout during brew/steam | ⬜ | Grinder has no reference to orchestrator; not yet wired |
| Hard over-temperature cutoff | 🚧 | Documented in example YAML via `on_value_range`; relies on native ESPHome climate action |
| Watchdog (heater-off on reset) | ✅ | Native ESPHome watchdog; SSR GPIO defaults LOW on reset |
| `safe_stop_all()` on brew/steam stop | ✅ | Closes all valves, stops all pumps immediately |

---

## Native ESPHome Features Used

These are **not** custom platforms — they are standard ESPHome entities used by the reference
configuration and documented in the example YAML.

| Feature | Status | Notes |
|---|---|---|
| PID temperature control (`climate.pid`) | ✅ | Full ESPHome PID with tunable kp/ki/kd and autotune button |
| Thermocouple support (MAX6675 / MAX31855) | ✅ | Both options documented in example; NTC also supported |
| SSR output (`output.slow_pwm`) | ✅ | 1 s period for SSR duty-cycle control |
| Home Assistant native API | ✅ | All entities auto-discovered; API encryption supported |
| OTA updates | ✅ | Standard ESPHome OTA via Wi-Fi |
| OLED display (SSD1306) | ✅ | Optional; display lambda documented in example YAML |
| PID autotune button | ✅ | Template button triggers `climate.pid.autotune` |

---

## Planned Features (No Code Yet)

| Feature | Target Phase | Notes |
|---|---|---|
| Brew profiles (`espresso_machine_profile:`) | Phase 12 | Multi-phase pressure/flow curves; runtime HA select |
| Gaggiuino/GaggiaMate profile import | Phase 12 | Python CLI converter planned |
| Shot stats as HA sensor entities | Phase 7 finish | time, volume, yield per shot |
| Cleanup scripts execution | Phase 9 | `cleanup_script:` blocks wired to ESPHome action lists |
| Steam flow-rate control (pump duty cycle) | Phase 8 | Pulse-width modulation of pump to maintain ml/s target |
| Grinder lockout via orchestrator | Phase 6 finish | Pass `EspressoMachine*` reference to grinder |
| Weight-based shot exit (scale) | Future | Requires HX711 / NAU7802 scale platform |
| Pressure transducer | Future | Requires ADC + transducer hardware |
