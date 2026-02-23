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
| P0-1 | Hard over-temperature cutoff (C++ test) | ⚠️ YAML only | mock_scenarios.md | Covered in example YAML via `on_value_range`; C++ orchestrator test planned but not written |
| ~~P0-2~~ | ~~Grinder lockout during brew/steam~~ | ✅ N/A | — | **Design decision:** Grinder and brew are independent operations; no lockout needed |
| P0-3 | Thermocouple/sensor fault handling | ⬜ Not done | failure_scenarios.md | PID could drive 100% duty if sensor returns 0/NaN; needs sensor-fault injection API |
| P0-4 | Brew timeout when HA/Wi-Fi disconnects | ⬜ Not done | failure_scenarios.md | Brew continues until `flow_max`; no watchdog timeout config |

### 🟠 P1 — High Priority (Blocks Functionality)

| ID | Feature / Scenario | Status | Source | Notes |
|----|-------------------|--------|--------|-------|
| P1-1 | Production `IHeater` adapter for ESPHome `climate.pid` | ⬜ Not done | PLAN.md Phase 8 | Steam works with `MockHeater`; needs real climate entity adapter |
| P1-2 | Heater setpoint wiring for brew mode (climate call) | 🚧 Partial | PLAN.md Phase 7 | Heater stored as `Component*`; `set_target_temperature()` not actually called |
| P1-3 | Temperature surfing: apply computed setpoint to climate | 🚧 Partial | FEATURES.md | Ramp value computed but NOT applied to climate entity |
| P1-4 | `esphome config` validation of heater section | ⬜ Not done | PLAN.md Phase 2 | Requires Phase 1 complete + real climate wiring test |
| P1-5 | Shot stats as HA sensor entities | ⬜ Not done | PLAN.md Phase 7 | Stats stored on orchestrator; not yet exposed as separate HA sensors |

### 🟡 P2 — Medium Priority (Usability / Completeness)

| ID | Feature / Scenario | Status | Source | Notes |
|----|-------------------|--------|--------|-------|
| P2-1 | Cleanup scripts execution (`cleanup_script:` blocks) | ⬜ Not done | PLAN.md Phase 9 | Schema accepts block; not executed |
| P2-2 | `espresso_machine.flush` helper action | ⬜ Not done | PLAN.md Phase 9 | Built-in action: pump N ml through purge valve |
| P2-3 | Document combining with ESPHome `script:` platform | ⬜ Not done | PLAN.md Phase 9 | Documentation task |
| P2-4 | Steam flow-rate control validation | ✅ Done | FEATURES.md | Bang-bang pump control to maintain `steam_flow_max_ml_per_s_` implemented |
| P2-5 | Residual flow after pump stop (C++ test) | ⬜ Not done | mock_scenarios.md | Volume should not overflow `flow_max + margin` |
| P2-6 | Power ON with thermoblock already at steam temp | ⬜ Not done | failure_scenarios.md | UX consideration — no interlock; user caution expected |
| P2-7 | Steam pump bang-bang: 2s minimum on window | ⬜ Not done | User request | Reduce pump wear by requiring minimum 2-second on time before toggling off |

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
| `Safety_OverTempCutoff` | Temperature exceeds 165 °C | Heater forced OFF immediately | ⚠️ Planned |
| `Safety_PIDNotResumeAfterCutoff` | PID tries to re-enable heater post-cutoff | Cutoff interlock blocks it | ⚠️ Planned |
| `Safety_HeaterOffOnReset` | ESP32 resets mid-brew | SSR defaults to LOW (heater off) | ✅ Hardware default |
| `Safety_WatchdogReboot` | Loop stalls > watchdog timeout | ESPHome resets; heater off | ✅ ESPHome built-in |
| `Safety_ValveInterlockEnforced` | Two valves open simultaneously | Second valve refused / first closed | ✅ Implemented |
| `Safety_BrewStopsAtFlowMax` | `flow_max` reached mid-shot | Brew stops pump and closes valve | ✅ Implemented |
| `Safety_ResidualFlowAfterStop` | Residual pressure drains after pump off | Volume not > `flow_max + margin` | ⚠️ Planned |
| `Safety_PurgeOnSteamStop` | Steam stopped by user | Purge valve opens; pressure released | ✅ Implemented |

---

## Summary by Priority

| Priority | Total | Done | Partial | Not Done |
|----------|-------|------|---------|----------|
| 🔴 P0 | 3 | 0 | 1 | 2 |
| 🟠 P1 | 5 | 0 | 2 | 3 |
| 🟡 P2 | 7 | 1 | 0 | 6 |
| 🟢 P3 | 7 | 0 | 2 | 5 |

**Next recommended actions:**
1. Complete P0-1: Add C++ orchestrator test for over-temperature cutoff
2. Complete P1-1/P1-2: Wire `IHeater` adapter for production `climate.pid` entity
3. Complete P0-4: Add brew timeout configuration for Wi-Fi disconnect scenario

