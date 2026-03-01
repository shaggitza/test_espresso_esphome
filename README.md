# ESPHome Espresso Machine Controller

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![ESPHome](https://img.shields.io/badge/ESPHome-External%20Component-green)](https://esphome.io/components/external_components/)

A fully-featured ESPHome **external component** that reimplements the low-voltage controller of a
semi-automatic espresso machine — designed specifically around the
**Philips Barista Brew** (integrated grinder, single thermoblock, steam wand, volumetric flow meter)
but written to be generic enough for other machines with similar hardware.

> ⚠️ **USE AT YOUR OWN RISK.** Working with espresso machines involves mains voltage on the high-voltage
> side. This project **only** addresses the low-voltage (3.3 V / 5 V) control side. Always maintain
> proper galvanic isolation between the LV controller board and the 220 V side. If you are unsure,
> do not proceed.

---

## Features

See [`FEATURES.md`](FEATURES.md) for the full, up-to-date feature status table (implemented /
partial / planned). A high-level summary:

| Feature | Status | Notes |
|---|---|---|
| **PID temperature control** | ✅ Implemented | Native ESPHome `climate.pid`; autotune supported |
| **Volumetric shot control** | ✅ Implemented | Flow meter with ISR pulse counter; auto-terminates at target volume |
| **Valve management + interlock** | ✅ Implemented | Named valves; single-open safety interlock at platform level |
| **Vibration pump control** | ✅ Implemented | Relay (on/off) and dimmer (0–100 %) types |
| **Grinder integration** | ✅ Implemented | Timed relay grind; adjustable from HA; no brew/steam lockout by design — grinder is an independent entity |
| **Pre-infusion** | ✅ Implemented | Volume-driven pre-wet + configurable hold time |
| **Brew temperature cooldown** | ✅ Implemented | Pre-brew cooldown when thermoblock is above target (e.g. after aborted steam): purge valve + pump active until temp drops to target, then HEATING proceeds |
| **Brew state machine** | ✅ Implemented | All states; heater setpoint wiring; temperature-gating; pre-infusion; optional temperature cooldown |
| **Steam mode** | ✅ Implemented | Full sequence: HEATING → PURGING → STEAMING → COOLING → CLEANUP; temperature-gated; purge-before-steam; safety timeout |
| **Steam pump minimum on-window** | ✅ Implemented | Pump owns bang-bang flow-rate control: orchestrator calls `set_target_flow()`; pump modulates on/off in its `loop()`, respecting `pump_min_on_time` / `pump_min_off_time` (P2-7) |
| **Temperature surfing** | ✅ Implemented | Configurable offset + ramp time; applied via `IHeater` on each brew tick |
| **Cleanup script callbacks** | ✅ Implemented | `cleanup_script:` fires at DONE/CLEANUP; reference any ESPHome script by id (P2-1) |
| **Maintenance flush action** | ✅ Implemented | `espresso_machine.flush`: pumps N ml through purge valve on demand (P2-2) |
| **Descale routine** | ✅ Implemented | `espresso_machine.descale_start`: automated N-cycle pump-on/soak sequence through purge valve; configurable cycle count, pump time, and soak time; stops with `descale_stop()` |
| **Backflush routine** | ✅ Implemented | `espresso_machine.backflush_start`: automated N-cycle pressurize/release sequence via brew valve (3-way solenoid); configurable cycle count and timing; stops with `backflush_stop()` |
| **Brew profiles** | ⬜ Planned | Multi-phase pressure/flow curves (Phase 12) |
| **Scale — Bluetooth** (Acaia Lunar, Bookoo, Felicita Arc, Difluid) | ⬜ Planned | ESP32 BLE connection; weight-based brew exit + grinder dosing (Phase 13); see `docs/scales.md` |
| **Scale — Wired load cell** (HX711 / NAU7802) | ⬜ Planned | Same `IScale` interface as BT variant; fully offline (Phase 13) |
| **Home Assistant integration** | ✅ Implemented | All entities auto-discovered via native ESPHome API |
| **OTA updates** | ✅ Implemented | Standard ESPHome OTA via Wi-Fi |

---

## Hardware Overview (Philips Barista Brew)

```
┌──────────────────────────────────────────────────────┐
│                  HIGH VOLTAGE SIDE (220 V)            │
│   ┌──────────┐  ┌──────────┐  ┌──────────────────┐  │
│   │ Thermoblock│  │  Pump    │  │   Grinder Motor  │  │
│   │  (heater)  │  │ (vibrtn) │  │  (relay)         │  │
│   └────┬─────┘  └────┬─────┘  └────────┬─────────┘  │
│        │SSR           │SSR/relay        │relay        │
└────────┼─────────────┼─────────────────┼─────────────┘
         │             │                 │
┌────────┼─────────────┼─────────────────┼─────────────┐
│                  LOW VOLTAGE SIDE (3.3/5 V)           │
│   ┌────▼─────────────▼─────────────────▼───────────┐ │
│   │               ESP32 controller board            │ │
│   │                                                 │ │
│   │  Thermocouple ──► MAX6675/MAX31855              │ │
│   │  Flow meter   ──► GPIO (pulse counting)         │ │
│   │  Valves       ──► GPIO → relay/MOSFET           │ │
│   └─────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────┘
```

---

## Quick Start

### 1. Wire up the controller board

See [`docs/wiring.md`](docs/wiring.md) for a full pin-out and connection diagram.

### 2. Copy the example configuration

```bash
cp examples/philips_barista_brew.yaml my_espresso.yaml
# Edit my_espresso.yaml to match your pin assignments and Wi-Fi credentials
```

### 3. Flash with ESPHome

```bash
esphome run my_espresso.yaml
```

### 4. Add to Home Assistant

The device will be discovered automatically via the ESPHome native API.

---

## Component YAML Reference

Each subsystem is a **first-class ESPHome entity** declared at the top level with its own
platform block. The `espresso_machine:` block is a pure orchestrator — it only references
other entities by `id:` and manages the brew/steam state machines.

```yaml
external_components:
  - source: github://shaggitza/test_espresso_esphome@main
    components:
      - espresso_machine           # orchestrator: brew + steam state machines
      - espresso_machine_valve     # solenoid valve with safety interlock
      - espresso_machine_pump      # vibration pump (relay or dimmer)
      - espresso_machine_grinder   # timed relay grinder with lockout
      - espresso_machine_flow_meter  # pulse-counting volumetric sensor

# Temperature sensor — native ESPHome (visible in HA, usable in automations)
sensor:
  - platform: max6675
    id: thermoblock_temp
    name: "Thermoblock Temperature"
    cs_pin: GPIO5

# Heater SSR output — native ESPHome slow_pwm
output:
  - platform: slow_pwm
    id: heater_ssr
    pin: GPIO4
    period: 1s           # initial period; override at runtime via the SSR Period number in HA

# Heater — native ESPHome PID climate (first-class citizen)
climate:
  - platform: pid
    id: main_heater
    name: "Espresso Heater"
    sensor: thermoblock_temp
    default_target_temperature: 90
    heat_output: heater_ssr
    control_parameters:
      kp: 2.5
      ki: 0.05
      kd: 15.0

# Heater controller adapter — espresso_machine_heater platform
# Bridges climate.pid to the orchestrator and exposes the SSR switching period
# as a Home Assistant number entity so you can tune PID responsiveness from HA
# without reflashing.  Shorter period = faster response; longer = less SSR wear.
espresso_machine_heater:
  id: heater_ctrl
  climate_id: main_heater
  ssr_output: heater_ssr         # wire slow_pwm output to enable HA period control
  ssr_default_period_ms: 1000   # initial period published to HA on boot (ms)
  ssr_period_number:
    name: "SSR Period"           # adjustable from HA — range 8–10 000 ms
    entity_category: diagnostic

# Flow meter — espresso_machine_flow_meter platform (first-class citizen)
espresso_machine_flow_meter:
  id: brew_flow
  name: "Brew Flow"
  pin: GPIO34
  pulses_per_ml: 0.5195
  rate_sensor:
    name: "Brew Flow Rate"        # ml/s — instantaneous rate
  total_sensor:
    name: "Brew Flow Total"       # mL accumulated this shot
  avg_rate_sensor:
    name: "Brew Flow Rate (3s avg)"  # ml/s — 3-second rolling average

# Valves — espresso_machine_valve platform (first-class citizens)
# Each valve is its own switch entity in HA; interlock is enforced by the platform.
espresso_machine_valve:
  - id: brew_valve
    name: "Brew Valve"
    pin: GPIO26
  - id: steam_valve
    name: "Steam Valve"
    pin: GPIO27
  - id: purge_valve
    name: "Purge / Drain Valve"
    pin: GPIO14

# Pump — espresso_machine_pump platform (first-class citizen)
# The pump owns the flow meter: it holds the sensor reference and exposes
# flow data through the IPump interface. This keeps the orchestrator agnostic
# of the flow meter type and makes future rotary-pump migration seamless.
espresso_machine_pump:
  id: main_pump
  name: "Vibration Pump"
  type: relay          # relay | dimmer
  pin: GPIO25
  flow_meter: brew_flow   # flow meter wired to this pump

# Grinder — espresso_machine_grinder platform (first-class citizen)
espresso_machine_grinder:
  id: main_grinder
  name: "Grinder"
  type: relay          # relay | none
  pin: GPIO23
  default_grind_time: 7s

# Espresso Machine — pure orchestrator, references all entities above by id:
espresso_machine:
  id: my_espresso
  power_switch: machine_power  # syncs HA switch on idle auto-off / steam cooldown
  brew:
    heater: main_heater
    pump: main_pump
    valve: brew_valve
    purge_valve: purge_valve
    target_temperature: 90°C
    temperature_profile:
      offset: 5°C
      ramp_time: 20s
    flow_max: 40ml
    flow_offset: 20ml
  steam:
    heater: main_heater
    pump: main_pump
    valve: steam_valve
    purge_valve: purge_valve
    target_temperature: 135°C
    purge_volume: 5ml      # flush residual water before opening steam valve
    flow_max: 2ml/s        # pump targets this flow rate via bang-bang in its own loop()
    cool_down_to: 90°C
    timeout: 5min          # optional safety auto-stop
    # NOTE: pump_min_on_time is configured on the espresso_machine_pump: entity,
    #       not here.  See the espresso_machine_pump: block below for that setting.
```

See [`examples/philips_barista_brew.yaml`](examples/philips_barista_brew.yaml) for the full annotated
example including pre-infusion, cleanup scripts, display, and Home Assistant automations.

---

## Project Structure

See [`structure.md`](structure.md) for the complete layout of source files and their roles.

---

## Roadmap / Plan

See [`PLAN.md`](PLAN.md) for the phased development plan.

---

## Feature Tracking

See [`FEATURES.md`](FEATURES.md) for the authoritative status of every feature (implemented,
partial, or planned). Developers and agents: **update `FEATURES.md` in the same PR** whenever
you implement or change a feature.

---

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md).

---

## License

Apache 2.0 — see [`LICENSE`](LICENSE).

---

## Acknowledgements & Inspiration

- [pico_espresso](https://github.com/vecinimod/pico_espresso) — Raspberry Pi Pico W based controller
  with PID, pump profiling, flow meter, pressure transducer, and web UI; a major source of inspiration
  for the feature set and hardware approach of this project.
- [ESPHome PID Climate](https://esphome.io/components/climate/pid/) — built-in PID loop used for
  temperature control.
- [ESPHome External Components](https://esphome.io/components/external_components/) — the plugin
  mechanism this project builds on.
