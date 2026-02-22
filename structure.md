# Project Structure

This document describes the layout of the repository and the role of every file and directory.

```
test_espresso_esphome/
│
├── README.md                        # Project overview, quick-start, YAML reference
├── PLAN.md                          # Phased development plan and roadmap
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
│   └── espresso_machine/            # ESPHome external component root
│       │
│       ├── __init__.py              # Top-level component schema and code-gen entry point
│       ├── espresso_machine.h       # C++ class: EspressoMachine (Component base)
│       ├── espresso_machine.cpp     # C++ implementation: setup(), loop(), state machine
│       │
│       ├── heater/
│       │   ├── __init__.py          # Schema: type, sensor_type, cs_pin, ssr_pin, pid {}
│       │   │                        #   (delegates PID logic to ESPHome's built-in climate.pid)
│       │   ├── heater.h             # ThermoblockHeater class; wraps PID climate
│       │   └── heater.cpp           # PID loop, safety cutoff, auto-tune trigger
│       │
│       ├── grinder/
│       │   ├── __init__.py          # Schema: type (relay|none), pin, default_grind_time
│       │   ├── grinder.h            # GrinderController class
│       │   └── grinder.cpp          # Timed relay logic, interlock with brew/steam
│       │
│       ├── flow_meter/
│       │   ├── __init__.py          # Schema: pin, pulses_per_ml
│       │   ├── flow_meter.h         # FlowMeter class; pulse counter + volume accumulator
│       │   └── flow_meter.cpp       # ISR pulse handler, flow rate calculation, calibration
│       │
│       ├── valve/
│       │   ├── __init__.py          # Schema: id, pin, normally_open (optional)
│       │   ├── valve.h              # Valve class; named GPIO output with safety interlock
│       │   └── valve.cpp            # open(), close(), interlock enforcement
│       │
│       ├── pump/
│       │   ├── __init__.py          # Schema: id, type (relay|dimmer), pin
│       │   ├── pump.h               # PumpController class
│       │   └── pump.cpp             # On/off relay or duty-cycle dimmer control
│       │
│       ├── brew/
│       │   ├── __init__.py          # Schema: heater, target_temperature, temperature_profile,
│       │   │                        #         flow_meter, flow_max, flow_offset, valve,
│       │   │                        #         purge_valve, pump, pre_infusion, cleanup_script
│       │   ├── brew.h               # BrewController class; shot state machine
│       │   └── brew.cpp             # State machine: idle→heating→pre_infusion→brewing→done→cleanup
│       │
│       └── steam/
│           ├── __init__.py          # Schema: heater, target_temperature, flow_max,
│           │                        #         cool_down_to, valve, purge_valve, pump
│           ├── steam.h              # SteamController class
│           └── steam.cpp            # Heat→steam→cool-down sequence, pump duty-cycle
│
├── examples/
│   └── philips_barista_brew.yaml    # Full annotated example for the Philips Barista Brew
│                                    # with integrated grinder — ready to flash
│
└── docs/
    ├── wiring.md                    # Pin-out, wiring diagrams, isolation notes
    ├── pid_tuning.md                # How to tune the PID for a thermoblock machine
    ├── home_assistant.md            # HA Lovelace dashboard YAML for the espresso machine
    └── troubleshooting.md           # Common issues and fixes
```

---

## Component Architecture

```
espresso_machine (top-level)
│
├── heater            — Temperature sensor + SSR + PID loop
│     wraps ESPHome climate.pid internally
│
├── grinder           — Relay-driven grinder with timed operation
│
├── flow_meter        — Pulse-counting flow sensor → ml accumulation
│
├── valve             — Named GPIO outputs (brew_valve, steam_valve, purge_valve)
│     enforces single-open interlock
│
├── pump              — Vibration pump control (relay or AC dimmer)
│
├── brew              — Shot state machine
│     orchestrates: heater setpoint → pre_infusion → pump + valve → flow_meter stop → cleanup
│
└── steam             — Steam state machine
      orchestrates: heater setpoint → steam valve → pump duty cycle → cool_down → purge
```

---

## Key Design Decisions

### External Component Pattern

The project follows ESPHome's [external component](https://esphome.io/components/external_components/)
pattern. Users reference it directly from GitHub in their YAML:

```yaml
external_components:
  - source: github://shaggitza/test_espresso_esphome@main
    components: [espresso_machine]
```

No local file copying is needed. ESPHome fetches the component at build time.

### PID Reuse

Rather than implementing a PID algorithm from scratch, the `heater` subcomponent delegates to
ESPHome's built-in `climate.pid` platform. This means PID auto-tune, deadband, and output averaging
are all available out of the box via the standard ESPHome PID parameters.

### State Machine in C++

The brew and steam sequences are implemented as C++ state machines in `loop()`. This keeps the
timing deterministic and avoids blocking the ESPHome event loop. ESPHome `script:` actions can
trigger state transitions from YAML.

### Named Valves

Valves are identified by their YAML `id:` string rather than by position. This allows users to
add, remove, or rename valves without breaking the brew/steam configuration.

### Flow Offset

`flow_offset` accounts for the volume of water absorbed by the coffee puck and basket that never
reaches the cup. It is subtracted from the raw flow meter reading when computing the yield displayed
in Home Assistant. Users can adjust it to match their basket size and dose.
