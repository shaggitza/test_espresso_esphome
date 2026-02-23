# Project Structure

This document describes the layout of the repository and the role of every file and directory.

```
test_espresso_esphome/
│
├── README.md                        # Project overview, quick-start, YAML reference
├── PLAN.md                          # Phased development plan and roadmap
├── TODO.md                          # Consolidated TODO list with priorities
├── FEATURES.md                      # Feature status tracking (authoritative)
├── structure.md                     # This file — repository layout documentation
├── CONTRIBUTING.md                  # How to contribute to the project
├── CHANGELOG.md                     # Version history
├── NOTICE                           # Apache 2.0 attribution notice
├── LICENSE                          # Apache 2.0 license text
├── .gitignore                       # Files excluded from version control
│
├── .github/
│   ├── copilot-instructions.md      # GitHub Copilot workspace context
│   └── workflows/
│       ├── validate.yml             # CI: ESPHome YAML validation + Python lint
│       └── release.yml             # CI: tag → GitHub Release automation
│
├── components/
│   └── espresso_machine/            # Pure orchestrator component (brew + steam state machines)
│       ├── __init__.py              # Top-level schema: brew {}, steam {} + entity id references
│       ├── espresso_machine.h       # EspressoMachine class: coordinates brew/steam/interlock
│       └── espresso_machine.cpp     # Brew state machine, steam state machine, safety interlocks
│
│   └── espresso_machine_valve/      # First-class solenoid valve platform
│       ├── __init__.py              # Schema: id, name, pin, normally_open
│       │                            # Registers as switch platform; enforces single-open interlock
│       ├── valve.h                  # Valve class (inherits Switch)
│       └── valve.cpp                # open(), close(), interlock with sibling valves
│
│   └── espresso_machine_pump/       # First-class vibration pump platform
│       ├── __init__.py              # Schema: id, name, type (relay|dimmer), pin
│       │                            # Registers as switch (relay) or number (dimmer) platform
│       ├── pump.h                   # PumpController class
│       └── pump.cpp                 # Relay on/off; duty-cycle via slow_pwm; run(volume_ml) action
│
│   └── espresso_machine_grinder/    # First-class grinder platform
│       ├── __init__.py              # Schema: id, name, type (relay|none), pin, default_grind_time
│       │                            # Registers as button (one-shot) + number (grind time) platform
│       ├── grinder.h                # GrinderController class
│       └── grinder.cpp              # Timed relay; refuses activation during brew/steam
│
│   └── espresso_machine_flow_meter/ # First-class volumetric flow sensor platform
│       ├── __init__.py              # Schema: id, name, pin, pulses_per_ml
│       │                            # Registers as sensor platform; exposes rate + total child sensors
│       ├── flow_meter.h             # FlowMeter class (inherits Sensor)
│       └── flow_meter.cpp           # ISR pulse counter, ml/s rate, total volume, reset action
│
│   └── espresso_machine_mock_heater/  # Mock heater for simulation (thermal ODE)
│       ├── __init__.py                # Schema: power_watts, thermal_mass, heat_loss, ambient_temp
│       │                              # Exposes output + sensor for PID; HA number entities for tuning
│       ├── mock_heater.h              # MockHeater, MockHeaterOutput, MockHeaterTempSensor classes
│       └── mock_heater.cpp            # Thermal ODE: dT/dt = (duty×P - h×ΔT) / C
│
│   └── espresso_machine_mock_pump/    # Mock pump for simulation (puck wetting model)
│       ├── __init__.py                # Schema: nominal_flow_ml_per_s, puck_time_constant_s
│       │                              # Registers as switch; exposes rate + total sensors
│       ├── mock_pump.h                # MockPump class (inherits Switch, implements IPump)
│       └── mock_pump.cpp              # Flow model: Q(t) = Q_nom × (1 − exp(−t/τ))
│
├── examples/
│   └── philips_barista_brew.yaml      # Full annotated example for the Philips Barista Brew
│                                      # with integrated grinder — ready to flash
│   └── philips_barista_brew_mock.yaml # Mock hardware version — no physical sensors needed
│                                      # PID and orchestrator unchanged; all physics HA-tunable
│   └── secrets.yaml.template          # Credentials template (secrets.yaml is git-ignored)
│
└── docs/
    ├── wiring.md                    # Pin-out, wiring diagrams, isolation notes
    ├── pid_tuning.md                # How to tune the PID for a thermoblock machine
    ├── home_assistant.md            # HA Lovelace dashboard YAML for the espresso machine
    └── troubleshooting.md           # Common issues and fixes
```

---

## Component Architecture

The fundamental design principle: **every hardware subsystem is a first-class ESPHome entity**.
ESPHome itself follows this pattern — sensors, switches, climate entities, and outputs are all
declared at the top level and wired together by `id:` references.
This project extends that pattern with espresso-machine-specific platforms.

```
Native ESPHome entities (standard top-level blocks)
  sensor:
    - platform: max6675        → thermoblock_temp   (temperature sensor → HA)
  output:
    - platform: slow_pwm       → heater_ssr         (SSR output)
  climate:
    - platform: pid            → main_heater        (PID controller → HA climate entity)

Custom espresso_machine_* platforms (each a top-level block, each a HA entity)
  espresso_machine_flow_meter  → brew_flow          (flow rate + volume → HA sensors)
  espresso_machine_valve       → brew_valve         (switch → HA; interlock enforced)
  espresso_machine_valve       → steam_valve        (switch → HA; interlock enforced)
  espresso_machine_valve       → purge_valve        (switch → HA; interlock enforced)
  espresso_machine_pump        → main_pump          (switch/number → HA)
  espresso_machine_grinder     → main_grinder       (button + number → HA)

Mock components for simulation (drop-in replacements for physical hardware)
  espresso_machine_mock_heater → mock_heater        (thermal ODE simulation; HA-tunable)
    └── output                 → heater_ssr         (PID heat_output references this)
    └── temperature_sensor     → thermoblock_temp   (PID sensor references this)
  espresso_machine_mock_pump   → main_pump          (puck wetting flow model; HA-tunable)
    └── rate_sensor + total_sensor                  (same interface as flow_meter)

Orchestrator (references all entities above by id:)
  espresso_machine             → my_espresso        (brew + steam state machines → HA)
```

```
Entity relationship diagram

  thermoblock_temp ──sensor──► main_heater (PID) ──output──► heater_ssr (SSR)
                                    │
                              setpoint changed by
                                    │
  brew_flow ────────────────► my_espresso ◄──────────────── main_grinder
  brew_valve ───────────────►  (orchestrator)  ◄──────────── main_pump
  steam_valve ──────────────►  brew / steam    ◄──────────── purge_valve
```

---

## Key Design Decisions

### First-Class Entity Pattern

Each hardware subsystem has its own top-level ESPHome platform block. This means:
- Valves, pump, grinder, and flow meter are **independently controllable** from HA and scripts.
- Users can mix and match: use `espresso_machine_valve` standalone without the full orchestrator.
- Platform-level safety (valve interlock, grinder lockout) is enforced regardless of whether
  the orchestrator is present.

### External Component Pattern

The project follows ESPHome's [external component](https://esphome.io/components/external_components/)
pattern. Users reference it directly from GitHub in their YAML:

```yaml
external_components:
  - source: github://shaggitza/test_espresso_esphome@main
    components:
      - espresso_machine
      - espresso_machine_valve
      - espresso_machine_pump
      - espresso_machine_grinder
      - espresso_machine_flow_meter
      # Mock components for simulation (optional — choose one or both):
      - espresso_machine_mock_heater   # Replaces SPI thermocouple + slow_pwm
      - espresso_machine_mock_pump     # Replaces pump + flow_meter
```

No local file copying is needed. ESPHome fetches the component at build time.

### PID Reuse

The heater uses ESPHome's built-in `climate.pid` platform directly — declared as a standard
`climate:` block in the user's YAML. The `espresso_machine` orchestrator changes the climate
entity's setpoint during brew/steam sequences. PID auto-tune, deadband, and output averaging
are all available out of the box.

### State Machine in C++

The brew and steam sequences are implemented as C++ state machines in `loop()`. This keeps the
timing deterministic and avoids blocking the ESPHome event loop. ESPHome `script:` actions can
trigger state transitions from YAML.

### Valve Interlock

The `espresso_machine_valve` platform maintains a registry of all declared valve instances.
When any valve is opened, the platform closes all other valves automatically (unless
`override: true` is set). This prevents brew and steam paths from being open simultaneously.

### Flow Offset

`flow_offset` accounts for the volume of water absorbed by the coffee puck and basket that
never reaches the cup. It is subtracted from the raw flow meter reading when computing the
yield displayed in Home Assistant. Users can adjust it to match their basket size and dose.
