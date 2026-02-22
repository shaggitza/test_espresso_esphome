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
| **Grinder integration** | ✅ Implemented | Timed relay grind; adjustable from HA; brew/steam lockout ⬜ pending |
| **Pre-infusion** | ✅ Implemented | Volume-driven pre-wet + configurable hold time |
| **Brew state machine** | 🚧 Partial | All states present; heater setpoint wiring + cleanup pending |
| **Steam mode** | 🚧 Partial | Valve + pump activation works; temperature-gating + flow control pending |
| **Temperature surfing** | 🚧 Partial | Config accepted; climate setpoint not yet applied at runtime |
| **Cleanup scripts** | ⬜ Planned | Schema accepts block; execution wired in Phase 9 |
| **Brew profiles** | ⬜ Planned | Multi-phase pressure/flow curves (Phase 12) |
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
    period: 1s

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

# Flow meter — espresso_machine_flow_meter platform (first-class citizen)
espresso_machine_flow_meter:
  id: brew_flow
  name: "Brew Flow"
  pin: GPIO34
  pulses_per_ml: 0.5195

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
espresso_machine_pump:
  id: main_pump
  name: "Vibration Pump"
  type: relay          # relay | dimmer
  pin: GPIO25

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
  brew:
    heater: main_heater
    pump: main_pump
    flow_meter: brew_flow
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
    flow_max: 2ml/s
    cool_down_to: 90°C
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
