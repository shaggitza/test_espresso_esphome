# Consolidated TODO — ESPHome Espresso Machine Controller

> **Auto-generated.** This file consolidates all planned features, safety scenarios, and
> outstanding tasks from `PLAN.md`, `FEATURES.md`, `docs/failure_scenarios.md`, and
> `docs/mock_scenarios.md`.

---

## Priority Legend

| Priority | Meaning |
|----------|---------|
| 🔴 P0 | Critical safety — must be done before any production use |
| 🟠 P1 | High — blocks major functionality or user experience |
| 🟡 P2 | Medium — nice-to-have, improves usability or completeness |
| 🟢 P3 | Low — future enhancement, polish, or convenience |

---

## Consolidated TODO List

### 🔴 P0 — Critical Safety

| ID | Feature / Scenario | Status | Source | Notes |
|----|-------------------|--------|--------|-------|
| P0-1 | Hard over-temperature cutoff (C++ test) | ✅ Done | mock_scenarios.md | `check_over_temp_safety_()` in orchestrator loop; latching cutoff flag; `Safety_OverTempCutoff` + `Safety_PIDNotResumeAfterCutoff` C++ tests added |
| ~~P0-2~~ | ~~Grinder lockout during brew/steam~~ | ✅ N/A | — | **Design decision:** Grinder and brew are independent operations; no lockout needed |
| P0-3 | Thermocouple/sensor fault handling | ✅ Done | failure_scenarios.md | NaN detection in `check_over_temp_safety_()`; `IHeater::force_off()` added; `Safety_SensorNaNForcesHeaterOff` C++ test added |
| P0-4 | Brew timeout when HA/Wi-Fi disconnects | ✅ Done | failure_scenarios.md | `set_brew_timeout_ms()` config; timeout checked in BREWING state; `Safety_BrewTimesOut*` C++ tests added |

### 🟠 P1 — High Priority (Blocks Functionality)

| ID | Feature / Scenario | Status | Source | Notes |
|----|-------------------|--------|--------|-------|
| P1-1 | Production `IHeater` adapter for ESPHome `climate.pid` | ✅ Done | PLAN.md Phase 8 | `espresso_machine_heater` component wraps `climate::Climate`; implements `get_current_temperature()`, `set_target_temperature()`, `force_off()` |
| P1-2 | Heater setpoint wiring for brew mode (climate call) | ✅ Done | PLAN.md Phase 7 | `brew_heater_ctrl_` (`IHeater*`) added; `set_target_temperature()` called in `brew_start()`; HEATING→BREWING gates on temperature when wired |
| P1-3 | Temperature surfing: apply computed setpoint to climate | ✅ Done | FEATURES.md | Ramp setpoint computed and applied via `brew_heater_ctrl_->set_target_temperature()` in BREWING state |
| P1-4 | `esphome config` validation of heater section | ✅ Done | PLAN.md Phase 2 | `heater_controller:` added to brew schema; `test_orchestrator.yaml` and `test_all_components.yaml` updated to validate `espresso_machine_heater` |
| P1-5 | Shot stats as HA sensor entities | ✅ Done | PLAN.md Phase 7 | `shot_stats.last_shot_time/volume/yield` in brew schema; `sensor::Sensor*` members published at BREWING→DONE |

### 🟡 P2 — Medium Priority (Usability / Completeness)

| ID | Feature / Scenario | Status | Source | Notes |
|----|-------------------|--------|--------|-------|
| P2-1 | Cleanup scripts execution (`cleanup_script:` blocks) | ✅ Done | PLAN.md Phase 9 | `set_brew_cleanup_fn()` / `set_steam_cleanup_fn()` called in DONE/CLEANUP states; C++ tests added |
| P2-2 | `espresso_machine.flush` helper action | ✅ Done | PLAN.md Phase 9 | `flush(volume_ml)` pumps N ml through brew purge valve; `FLUSHING` mode; `@automation.register_action`; C++ tests added |
| P2-3 | Document combining with ESPHome `script:` platform | ✅ Done | PLAN.md Phase 9 | Documented in `docs/home_assistant.md` with examples of `cleanup_script:` + `espresso_machine.flush` |
| P2-4 | Steam flow-rate control validation | ✅ Done | FEATURES.md | Bang-bang pump control to maintain `steam_flow_max_ml_per_s_` implemented |
| P2-5 | Residual flow after pump stop (C++ test) | ✅ Done | mock_scenarios.md | `Safety.ResidualFlowAfterStop` test verifies brew valve stays closed after flow_max; valve does not reopen |
| P2-6 | Power ON with thermoblock already at steam temp | ✅ Done | failure_scenarios.md | Documented in `docs/failure_scenarios.md`; `heater_controller:` handles cooling gate; no hard interlock by design |
| P2-7 | Steam pump bang-bang: 2s minimum on window | ✅ Done | User request | `steam_pump_min_on_ms_` (default 2000 ms); configurable via `pump_min_on_time:` in steam schema; C++ tests added |

### 🟢 P3 — Low Priority (Future / Polish)

| ID | Feature / Scenario | Status | Source | Notes |
|----|-------------------|--------|--------|-------|
| P3-1 | Brew profiles (`espresso_machine_profile:`) | ⬜ Not done | PLAN.md Phase 12 | Multi-phase pressure/flow curves; runtime HA select |
| P3-2 | Gaggiuino/GaggiaMate profile import (converter) | ⬜ Not done | PLAN.md Phase 12 | Python CLI converter planned |
| P3-3 | Weight-based shot exit (scale platform) | ⬜ Not done | PLAN.md future | Requires HX711/NAU7802 scale platform |
| P3-4 | Pressure transducer integration | ⬜ Not done | PLAN.md future | Requires ADC + transducer hardware |
| P3-5 | Display & UI (OLED lambda documentation) | 🚧 Partial | PLAN.md Phase 10 | Display lambda in example YAML; full docs not written |
| P3-6 | Complete wiring diagrams | 🚧 Partial | PLAN.md Phase 11 | `docs/wiring.md` exists but missing diagrams |
| P3-7 | Tag v1.0.0 release | ⬜ Not done | PLAN.md Phase 11 | Waiting for all P0/P1 items |

---

## C++ Safety Tests — Status

From `docs/mock_scenarios.md`:

| Test ID | Scenario | Expected Outcome | Status |
|---------|----------|------------------|--------|
| `Safety_OverTempCutoff` | Temperature exceeds 165 °C | Heater forced OFF immediately | ✅ Implemented |
| `Safety_PIDNotResumeAfterCutoff` | PID tries to re-enable heater post-cutoff | Cutoff interlock blocks it | ✅ Implemented |
| `Safety_HeaterOffOnReset` | ESP32 resets mid-brew | SSR defaults to LOW (heater off) | ✅ Hardware default |
| `Safety_WatchdogReboot` | Loop stalls > watchdog timeout | ESPHome resets; heater off | ✅ ESPHome built-in |
| `Safety_ValveInterlockEnforced` | Two valves open simultaneously | Second valve refused / first closed | ✅ Implemented |
| `Safety_BrewStopsAtFlowMax` | `flow_max` reached mid-shot | Brew stops pump and closes valve | ✅ Implemented |
| `Safety_ResidualFlowAfterStop` | Residual pressure drains after pump off | Volume not > `flow_max + margin` | ✅ Implemented |
| `Safety_PurgeOnSteamStop` | Steam stopped by user | Purge valve opens; pressure released | ✅ Implemented |

---

## Summary by Priority

| Priority | Total | Done | Partial | Not Done |
|----------|-------|------|---------|----------|
| 🔴 P0 | 3 | 3 | 0 | 0 |
| 🟠 P1 | 5 | 5 | 0 | 0 |
| 🟡 P2 | 7 | 7 | 0 | 0 |
| 🟢 P3 | 7 | 0 | 2 | 5 |

**Next recommended actions:**
1. Complete P3-5: Display & UI documentation
2. Complete P3-6: Wiring diagrams
3. Complete P3-7: Tag v1.0.0 release

