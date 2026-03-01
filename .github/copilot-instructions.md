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
| `components/espresso_machine/` | Orchestrator platform (brew + steam state machines) |
| `components/espresso_machine_valve/` | Solenoid valve platform (switch + interlock) |
| `components/espresso_machine_pump/` | Pump platform (switch or number) |
| `components/espresso_machine_grinder/` | Grinder platform (button + number) |
| `components/espresso_machine_flow_meter/` | Flow sensor platform (rate + volume sensors) |
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
- Each platform lives in its own namespace: `esphome::espresso_machine`,
  `esphome::espresso_machine_valve`, `esphome::espresso_machine_pump`, etc.
- Platform classes inherit from the appropriate ESPHome base:
  - Valve → `esphome::switch_::Switch`
  - Pump → `esphome::switch_::Switch` (relay) or `esphome::number::Number` (dimmer)
  - Grinder → `esphome::button::Button` + `esphome::number::Number`
  - Flow meter → `esphome::sensor::Sensor`
  - Orchestrator → `esphome::Component`
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

Every hardware subsystem is a **first-class ESPHome entity** declared at the top level with its
own platform block — exactly as ESPHome itself declares sensors, switches, and climate entities.
The `espresso_machine:` block is a **pure orchestrator**: it owns no hardware directly and only
references other entities by `id:`.

```yaml
# Temperature sensor — native ESPHome platform (top-level, first-class)
sensor:
  - platform: max6675
    id: thermoblock_temp

# Heater SSR — native ESPHome output
output:
  - platform: slow_pwm
    id: heater_ssr
    pin: GPIO4
    period: 1s

# Heater PID — native ESPHome climate (top-level, first-class)
climate:
  - platform: pid
    id: main_heater
    sensor: thermoblock_temp
    heat_output: heater_ssr

# Flow meter — custom espresso_machine_flow_meter platform (top-level, first-class)
espresso_machine_flow_meter:
  id: brew_flow
  pin: GPIO34
  pulses_per_ml: 0.5195

# Valves — custom espresso_machine_valve platform (each one top-level, first-class)
espresso_machine_valve:
  - id: brew_valve
    pin: GPIO26
  - id: steam_valve
    pin: GPIO27
  - id: purge_valve
    pin: GPIO14

# Pump — custom espresso_machine_pump platform (top-level, first-class)
espresso_machine_pump:
  id: main_pump
  type: relay
  pin: GPIO25

# Grinder — custom espresso_machine_grinder platform (top-level, first-class)
espresso_machine_grinder:
  id: main_grinder
  type: relay
  pin: GPIO23
  default_grind_time: 7s

# Orchestrator — references all entities above by id:
espresso_machine:
  id: my_espresso
  brew:
    heater: main_heater
    pump: main_pump
    flow_meter: brew_flow
    valve: brew_valve
    purge_valve: purge_valve
    target_temperature: 90°C
    flow_max: 40ml
    flow_offset: 20ml
    temperature_cooldown: false  # if true, cool to target_temperature before brewing when thermoblock is too hot
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
    # NOTE: pump_min_on_time is set on the espresso_machine_pump: entity (see above),
    #       not here in the steam section.
  descale:
    cycles: 3              # number of pump-on / soak cycles
    pump_time: 30s         # how long to pump descaling solution per cycle
    soak_time: 30s         # how long to soak between cycles
  backflush:
    cycles: 5              # number of pressurize / release cycles (blind filter required)
    pressurize_time: 10s   # how long to pump per cycle (builds back-pressure)
    release_time: 10s      # how long to release per cycle (3-way solenoid expels residue)
```

## Current Development Phase

See `PLAN.md` for the full phased roadmap.
**Phase 0 (initialisation) is complete.** The next step is Phase 1: component scaffold.

## Do Not

- Do not use `delay()` in C++ loop code.
- Do not commit `secrets.yaml`.
- Do not hardcode pin numbers in C++ — always route through the Python schema.
- Do not reinvent PID — delegate to `esphome::climate::PIDClimate`.
- Do not add dependencies not already available in ESPHome core.

## CI / Testing — Keeping Tests Green

The CI workflow (`.github/workflows/validate.yml`) runs these jobs on every PR:

| Job | What it validates |
|---|---|
| **Lint Python** | `flake8 components/ --max-line-length=100` |
| **ESPHome config** | `esphome config` on every YAML in `tests/` and `examples/` |
| **ESPHome compile** | `esphome compile tests/test_compile.yaml` (host platform) |
| **C++ unit tests** | GoogleTest suite in `tests/cpp/` via CMake |
| **Addon build** | Docker build of `addons/espresso_shot_history/` |

### Secrets handling

Example YAML configs use `!secret` references (`wifi_ssid`, `wifi_password`, `ha_api_key`,
`ota_password`). ESPHome resolves `secrets.yaml` **relative to the config file's directory**,
not the working directory. The CI must therefore provide `secrets.yaml` in every directory
that contains YAML configs using `!secret`:

- `secrets.yaml` (repo root — for test configs if any reference secrets)
- `examples/secrets.yaml` (for example configs)

The CI step copies `secrets.yaml.example` to both locations before running `esphome config`.
Both `secrets.yaml` and `examples/secrets.yaml` are gitignored — **never commit real secrets**.

### Mock secret values must be valid

ESPHome validates secret values during `esphome config`. Mock values in `secrets.yaml.example`
must satisfy all ESPHome validators:

- `ha_api_key` must be a **base64-encoded 32-byte** value (use `esphome generate-api-key`)
- `wifi_ssid` / `wifi_password` / `ota_password` can be any non-empty string

If you change or add `!secret` references, update `secrets.yaml.example` with valid mock values
and verify locally: `cp secrets.yaml.example secrets.yaml && cp secrets.yaml.example examples/secrets.yaml && esphome config examples/philips_barista_brew.yaml`

### Example YAML configs must validate offline

The `esphome config` step runs in CI without network access to external resources. Ensure:

- **No Google Fonts downloads** — comment out `font:` blocks using `gfonts://` URIs
  (or provide a local fallback font file checked into the repo).
- **No external `!include` files** that won't exist in CI.
- **ESP32 GPIO constraints** — GPIO34–39 are input-only with no internal pull resistors;
  use `mode: INPUT` (not `INPUT_PULLUP` or `INPUT_PULLDOWN`).
- **Unimplemented features** — if a schema field uses `cv.Any()` as a placeholder (e.g.
  `cleanup_script`), keep the corresponding YAML commented out until the feature is wired.

### C++ includes

When using standard library functions (`std::exp`, `std::sqrt`, etc.) in C++ component files,
always include the corresponding header (`<cmath>`, `<cstring>`, etc.). The ESPHome Arduino
framework may not transitively include them, causing GoogleTest builds to fail.

## Documentation and YAML Files — Always Keep Updated

> **Important:** Every code change that adds or modifies a feature **must** be accompanied by
> updates to all of the following files in the same PR. Reviewers will check for this.

| File | What to update |
|---|---|
| `FEATURES.md` | Feature status table (✅ / 🚧 / ⬜); add new rows for new features |
| `README.md` | Feature summary table; YAML reference snippet |
| `examples/philips_barista_brew.yaml` | Add new YAML config keys with inline comments |
| `examples/philips_barista_brew_mock.yaml` | Mirror all steam/brew config changes from the real example |
| `docs/failure_scenarios.md` | Add scenario rows; update C++ test coverage table |
| `docs/mock_scenarios.md` | Update scenario coverage matrix and safety testing plan |
| `.github/copilot-instructions.md` | Update YAML snippet when new top-level config keys are added |

**Rule:** No feature may be described as ✅ in `FEATURES.md` unless:
1. The C++ runtime code is complete and correct.
2. Unit tests exist and pass (`tests/cpp/`).
3. The feature is documented in the relevant docs files.
4. The example YAML files show how to configure it.
