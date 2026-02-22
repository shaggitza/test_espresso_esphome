# GitHub Copilot Instructions — ESPHome Espresso Machine Controller

## Project Purpose

This repository implements a **reusable ESPHome external component** that replaces the low-voltage
controller of a semi-automatic espresso machine (reference hardware: Philips Barista Brew).
The goal is a declarative YAML interface that any ESPHome user can adopt for their machine.

## Technology Stack

- **ESPHome** external component (Python schema + C++ runtime)
- **Python 3** — schema validation, code generation (`components/**/__init__.py`)
- **C++ (Arduino/ESP-IDF)** — runtime logic (`*.h`, `*.cpp` under `components/`)
- **YAML** — user-facing configuration and example device files
- **Home Assistant** — target integration platform (via ESPHome native API)

## Repository Layout

See `structure.md` for the full layout. Key directories:

| Path | Contents |
|---|---|
| `components/espresso_machine/` | ESPHome external component source (Python + C++) |
| `examples/` | Ready-to-flash YAML device configurations |
| `docs/` | Wiring guides, PID tuning, HA dashboard, troubleshooting |

## ESPHome External Component Conventions

When writing or modifying component files, follow these conventions:

### Python schema files (`__init__.py`)
- Import from `esphome.config_validation as cv` and `esphome.codegen as cg`.
- Define `CONFIG_SCHEMA` using `cv.Schema({...})`.
- Use `cv.Required(...)` / `cv.Optional(..., default=...)` for every key.
- The async `to_code(config)` function generates C++ calls (`cg.add(...)`).
- Sub-component schemas are composed into the parent with `cv.Schema.extend(...)`.

### C++ files (`.h` / `.cpp`)
- All classes live in namespace `esphome::espresso_machine`.
- Classes inherit from `esphome::Component` (or a suitable sub-base).
- Implement `setup()` for one-time initialisation and `loop()` for periodic work.
- Use state machines in `loop()` — never `delay()` or blocking calls.
- Follow ESPHome naming: `snake_case` for methods, `UPPER_SNAKE_CASE` for constants.
- 2-space indentation, matching ESPHome core style.

### Safety rules (always enforce)
- Heater SSR defaults to OFF (GPIO LOW) on reset and watchdog timeout.
- Hard over-temperature limit checked on every temperature reading, independent of PID.
- Valve interlock: only one valve open at a time (raise an error if violated in schema).
- Grinder must not operate during an active brew or steam sequence.

## Key YAML Concepts

The top-level `espresso_machine:` block wires together the subsystems:

```yaml
espresso_machine:
  heater:    { ... }   # thermoblock + PID
  grinder:   { ... }   # relay-driven grinder
  flow_meter:{ ... }   # pulse-counting flow sensor
  pump:      { ... }   # vibration pump
  valves:    [ ... ]   # named valves
  brew:      { ... }   # shot state machine config
  steam:     { ... }   # steam state machine config
```

Sub-blocks reference each other by `id:` string — e.g. `brew.heater: main_heater`.

## Current Development Phase

See `PLAN.md` for the full phased roadmap.
**Phase 0 (initialisation) is complete.** The next step is Phase 1: component scaffold.

## Do Not

- Do not use `delay()` in C++ loop code.
- Do not commit `secrets.yaml`.
- Do not hardcode pin numbers in C++ — always route through the Python schema.
- Do not reinvent PID — delegate to `esphome::climate::PIDClimate`.
- Do not add dependencies not already available in ESPHome core.
