# Supporting Other Machine Types — Plan & Architecture

> **Status: Planning — design document only, no code written yet.**
> This document maps the current component architecture to a range of machine hardware
> configurations — from bare-minimum relay control to high-end dual-boiler profiling
> machines — and defines the new components and orchestrator extensions needed to
> support each one.

---

## Table of Contents

1. [Hardware Capability Spectrum](#1-hardware-capability-spectrum)
2. [Machine Type Matrix](#2-machine-type-matrix)
3. [Minimum Viable Configuration (No Sensors)](#3-minimum-viable-configuration-no-sensors)
4. [Single Boiler Machines (e.g. Gaggia Classic)](#4-single-boiler-machines-eg-gaggia-classic)
5. [GaggiaMate Drop-In Replacement Firmware](#5-gaggiamate-drop-in-replacement-firmware)
6. [Dual Thermoblock Machines](#6-dual-thermoblock-machines)
7. [Dual Boiler Machines](#7-dual-boiler-machines)
8. [Dual NTC, Single Thermoblock](#8-dual-ntc-single-thermoblock)
9. [Dual Pump, Single Thermoblock (Cold Water Injection)](#9-dual-pump-single-thermoblock-cold-water-injection)
10. [New Components Required](#10-new-components-required)
11. [Orchestrator Extensions Required](#11-orchestrator-extensions-required)
12. [Component Reuse Summary](#12-component-reuse-summary)
13. [Phased Implementation Plan](#13-phased-implementation-plan)
14. [Open Questions](#14-open-questions)

---

## 1  Hardware Capability Spectrum

Every machine configuration sits somewhere on this spectrum.  The goal is to support
**all** of it — from bare-minimum relay control to dual-boiler multi-sensor profiling:

```
MINIMUM                                                              MAXIMUM
   │                                                                    │
   ▼                                                                    ▼
Pump relay                                                Dual boiler
+ valve relay                                             + dual thermoblock
(no temp sensor)                                          + dual NTC each
(pressurestat controls heat)                              + flow meter
(time-based shot only)                                    + scale
                                                          + pressure transducer
                                                          + display
                                                          + cloud logging
```

**Design rule:** every sensor and actuator is optional.  The orchestrator must degrade
gracefully when hardware is absent, using safe fallback behaviour.

---

## 2  Machine Type Matrix

| Machine type | Example models | Heater topology | Temp sensor | Flow meter | Grinder | Pump(s) |
|---|---|---|---|---|---|---|
| **Single thermoblock** (current) | Philips Barista Brew | 1 × thermoblock + SSR | 1 × K-type TC | 1 × AB32 | Integrated relay | 1 × vibration |
| **Single boiler — pressurestat** | Gaggia Classic (stock) | 1 × boiler, pressurestat | None (or NTC added) | None (or added) | None | 1 × vibration |
| **Single boiler — SSR bypass** | Gaggia Classic (modded), Rancilio Silvia | 1 × boiler + SSR | 1 × NTC or TC | Optional | None | 1 × vibration |
| **GaggiaMate target** | Gaggia Classic, Breville Barista Express | 1 × boiler + SSR | 1 × NTC | Optional | Optional | 1 × vibration |
| **Dual NTC, single thermoblock** | Custom Philips mod | 1 × thermoblock + SSR | 2 × NTC at different positions | 1 × AB32 | Optional | 1 × vibration |
| **Dual pump, single thermoblock** | Custom cooling mod | 1 × thermoblock + SSR | 1 × sensor | 1 × AB32 | Optional | 2 × vibration (brew + cool) |
| **Dual thermoblock** | DeLonghi Dinamica Pro | 2 × thermoblock + 2 × SSR | 2 × sensors | Optional | Optional | 1 × vibration |
| **Dual boiler** | Breville Dual Boiler, ECM Synchronika, Profitec Pro 700 | 2 × boiler + 2 × SSR | 2 × sensors | Optional | None | 1 × vibration |

---

## 3  Minimum Viable Configuration (No Sensors)

### 3.1  Target use case

A machine where the temperature is managed entirely by the built-in pressurestat (or
steam-rated thermostat) — no NTC probe, no thermocouple, no flow meter.  The ESP32
controls only the **pump relay, solenoid valve relay, and machine power**.

Shot timing is purely time-based: the user taps a button for N seconds.

### 3.2  What works today (zero new code)

```yaml
# Minimum viable configuration — no temp sensor, no flow meter
external_components:
  - source: github://shaggitza/test_espresso_esphome@main
    components:
      - espresso_machine
      - espresso_machine_valve
      - espresso_machine_pump

espresso_machine_valve:
  - id: brew_valve
    pin: GPIO26
  - id: purge_valve
    pin: GPIO14

espresso_machine_pump:
  id: main_pump
  type: relay
  pin: GPIO25

espresso_machine:
  id: my_espresso
  brew:
    pump: main_pump
    valve: brew_valve
    purge_valve: purge_valve
    flow_max: 999ml            # effectively disable flow-based stop
    flow_offset: 0ml
    # heater: not wired — no IHeater, no temperature gating
    # No flow_meter wired — brew stops only via brew_stop action or timeout
```

In HA, the user creates a button that calls `espresso_machine.brew_start` and a timer
automation that calls `espresso_machine.brew_stop` after the desired shot duration.

### 3.3  Required schema changes (small)

- `flow_max` and `flow_offset` should have sensible defaults (e.g. `999ml` and `0ml`)
  so they do not need to be specified when no flow meter is present.
- A new `brew_timeout` key should stop the shot automatically without needing an
  external HA automation.
- `valve` should become optional in `brew:` (some minimalist builds use a single
  multi-way solenoid externally controlled).

### 3.4  Fallback behaviour table

| Hardware missing | Fallback |
|---|---|
| No `heater_controller:` | `HEATING` state skipped immediately; brew/steam starts without temperature gating |
| No `flow_meter:` on pump | Flow-based termination disabled; only time/manual stop works |
| No `purge_valve:` | Purge phases skipped; brew/steam path not flushed |
| No `valve:` in brew | Pump runs without opening a brew valve (useful if machine has internal solenoid) |
| No `grinder:` | Grinder entity simply not created; brew/steam unaffected |

---

## 4  Single Boiler Machines (e.g. Gaggia Classic)

### 4.1  Hardware topology

```
┌────────────────────────────────────────────┐
│ HIGH VOLTAGE                                │
│  ┌──────────────────────────────────────┐  │
│  │       Boiler (brass, 200–300 mL)     │  │
│  │   ┌───────────────────┐             │  │
│  │   │  Heating element  │◄── SSR       │  │
│  │   └───────────────────┘             │  │
│  │   ┌───────────────────┐             │  │
│  │   │  Pressurestat /   │             │  │
│  │   │  NTC probe mount  │             │  │
│  │   └───────────────────┘             │  │
│  └──────────────────────────────────────┘  │
│  ┌──────────┐  ┌─────────────────────────┐ │
│  │  Pump    │  │  3-way solenoid valve    │ │
│  └──────────┘  └─────────────────────────┘ │
└────────────────────────────────────────────┘
```

Key differences from the Philips Barista Brew:
- **Single boiler** serves both brew and steam — the same water volume is used for both.
- **Temperature must change** between brew mode (~93 °C) and steam mode (~125–130 °C).
  This is the classic "temperature surfing" problem.
- **3-way solenoid valve** — one valve handles brew/bypass/return; not three separate valves.
- **Pressurestat** — the stock thermostat is a pressurestat that opens/closes at a set
  pressure.  It can be bypassed with an SSR for PID control.
- **No integrated grinder** on most single-boiler machines.
- **No flow meter** in stock form — can be added.

### 4.2  What works today (no new code)

With a Gaggia Classic fitted with an NTC probe + SSR bypass:

```yaml
external_components:
  - source: github://shaggitza/test_espresso_esphome@main
    components:
      - espresso_machine
      - espresso_machine_valve
      - espresso_machine_pump
      - espresso_machine_heater

sensor:
  - platform: ntc
    id: boiler_temp
    sensor: boiler_ntc_raw
    calibration:
      b_constant: 3950
      reference_temperature: 25°C
      reference_resistance: 10kOhm

output:
  - platform: slow_pwm
    id: heater_ssr
    pin: GPIO4
    period: 2s       # larger boiler benefits from longer period

climate:
  - platform: pid
    id: boiler_heater
    sensor: boiler_temp
    heat_output: heater_ssr
    default_target_temperature: 93°C
    pid:
      kp: 0.4
      ki: 0.004
      kd: 0.0

espresso_machine_heater:
  id: heater_ctrl
  climate_id: boiler_heater

espresso_machine_valve:
  - id: brew_valve
    pin: GPIO26
  - id: purge_valve
    pin: GPIO14

espresso_machine_pump:
  id: main_pump
  type: relay
  pin: GPIO25

espresso_machine:
  id: my_espresso
  brew:
    heater: boiler_heater
    heater_controller: heater_ctrl
    pump: main_pump
    valve: brew_valve
    purge_valve: purge_valve
    target_temperature: 93°C
    flow_max: 999ml            # disable flow-based stop if no flow meter
    flow_offset: 0ml
  steam:
    heater: boiler_heater
    heater_controller: heater_ctrl
    pump: main_pump
    valve: brew_valve          # same valve — single boiler machines share the valve
    purge_valve: purge_valve
    target_temperature: 128°C
    flow_max: 2ml/s
    cool_down_to: 93°C
    timeout: 5min
```

### 4.3  Pressurestat mode (no SSR bypass)

When the pressurestat is left intact (no SSR), the heater block is simply omitted.
The machine controls its own temperature.  The ESP32 still controls the pump and valve.

No `espresso_machine_heater`, no `climate.pid`, no `heater_controller:` in either
brew or steam.  Both `HEATING` states are skipped immediately (no temperature gating).

### 4.4  Required changes (minor)

- `valve:` in `brew:` and `steam:` should be optional (or allow sharing the same `id:`).
- `flow_max` / `flow_offset` need defaults so they can be omitted.
- `steam: valve:` currently requires a different valve than `brew: valve:`.  For 3-way
  solenoid machines, they should be allowed to share the same valve `id:`.

---

## 5  GaggiaMate Drop-In Replacement Firmware

### 5.1  What GaggiaMate is

[GaggiaMate](https://github.com/jniebuhr/gaggimate) is an open-source project that
provides a custom PCB (ESP32-based) and firmware for the **Gaggia Classic** (and similar
Gaggia / Breville machines).  The GaggiaMate PCB plugs into the stock wiring harness of
the machine.

The goal of this section is: **use this ESPHome firmware as a drop-in alternative
to the official GaggiaMate firmware on the GaggiaMate PCB** — giving users Home
Assistant integration, OTA updates, and ESPHome's full automation capabilities while
keeping their existing hardware.

### 5.2  GaggiaMate hardware (rev 1 / rev 2 PCB)

| Signal | GaggiaMate GPIO | Notes |
|---|---|---|
| Boiler heater SSR | GPIO16 | DC-control SSR input |
| Pump relay | GPIO17 | Vibration pump |
| 3-way solenoid valve | GPIO18 | Single solenoid valve |
| NTC sensor (boiler) | GPIO34 (ADC1_CH6) | 10 kΩ NTC, 3.3 V divider |
| Pressure transducer | GPIO35 (ADC1_CH7) | 0–16 bar, 0.5–4.5 V (optional) |
| Rotary encoder A | GPIO32 | UI control knob |
| Rotary encoder B | GPIO33 | UI control knob |
| Rotary encoder button | GPIO27 | Push to confirm |
| OLED SDA | GPIO21 | SSD1306 / SH1106 128×64 |
| OLED SCL | GPIO22 | I²C |
| Flow meter pulse | GPIO19 | Optional AB32 or similar |

> Pin assignments are approximate — verify against your GaggiaMate PCB version.
> The GaggiaMate project documents the exact pinout in its hardware repository.

### 5.3  Example YAML for GaggiaMate PCB

```yaml
# ESPHome drop-in for GaggiaMate PCB (Gaggia Classic / Breville Barista Express)
external_components:
  - source: github://shaggitza/test_espresso_esphome@main
    components:
      - espresso_machine
      - espresso_machine_valve
      - espresso_machine_pump
      - espresso_machine_heater
      - espresso_machine_flow_meter  # only if flow meter is wired

esphome:
  name: gaggiamate_esphome
  friendly_name: "GaggiaMate (ESPHome)"

esp32:
  board: esp32dev

# NTC boiler temperature
sensor:
  - platform: ntc
    id: boiler_temp
    sensor: boiler_ntc_raw
    calibration:
      b_constant: 3950
      reference_temperature: 25°C
      reference_resistance: 10kOhm
  - platform: resistance
    id: boiler_ntc_raw
    sensor: boiler_ntc_adc
    configuration: DOWNSTREAM
    resistor: 10kOhm
  - platform: adc
    id: boiler_ntc_adc
    pin: GPIO34
    attenuation: 11db
    update_interval: 500ms

output:
  - platform: slow_pwm
    id: heater_ssr
    pin: GPIO16       # GaggiaMate heater SSR GPIO
    period: 2s

climate:
  - platform: pid
    id: boiler_heater
    sensor: boiler_temp
    heat_output: heater_ssr
    default_target_temperature: 93°C
    pid:
      kp: 0.3
      ki: 0.003
      kd: 0.0

espresso_machine_heater:
  id: heater_ctrl
  climate_id: boiler_heater

espresso_machine_valve:
  - id: brew_valve
    pin: GPIO18       # GaggiaMate 3-way solenoid — single entity shared for brew + purge

espresso_machine_pump:
  id: main_pump
  type: relay
  pin: GPIO17         # GaggiaMate pump relay GPIO

# Optional flow meter (if fitted — requires a Y-splitter on the water path)
# espresso_machine_flow_meter:
#   id: brew_flow
#   pin: GPIO19
#   pulses_per_ml: 0.5195
#   rate_sensor:
#     name: "Flow Rate"
#   total_sensor:
#     name: "Shot Volume"

espresso_machine:
  id: my_espresso
  brew:
    heater: boiler_heater
    heater_controller: heater_ctrl
    pump: main_pump
    valve: brew_valve
    purge_valve: brew_valve       # same valve
    target_temperature: 93°C
    flow_max: 999ml               # disable if no flow meter; set to real value if fitted
    flow_offset: 0ml
  steam:
    heater: boiler_heater
    heater_controller: heater_ctrl
    pump: main_pump
    valve: brew_valve             # same single solenoid
    purge_valve: brew_valve
    target_temperature: 128°C
    flow_max: 2ml/s
    cool_down_to: 93°C
    timeout: 5min
```

### 5.4  What works today vs. what is missing

| Feature | Status | Notes |
|---|---|---|
| Boiler NTC temp sensor | ✅ Native ESPHome `ntc` sensor | No new components needed |
| SSR heater control via PID | ✅ Native ESPHome `climate.pid` | No new components needed |
| Pump relay | ✅ `espresso_machine_pump` | |
| 3-way solenoid valve | ✅ `espresso_machine_valve` | Share same `id:` for brew + purge |
| Brew start/stop from HA | ✅ `espresso_machine.brew_start` | |
| Steam start/stop from HA | ✅ `espresso_machine.steam_start` | |
| OLED display (read-only status) | ✅ Native ESPHome `display` | Lambda shows temp, mode, shot volume; see the main example YAML |
| Rotary encoder + interactive OLED menu | ⬜ Not yet | Native ESPHome `rotary_encoder` + `display` lambda; no menu framework yet |
| Pressure transducer | ⬜ Not yet | Native ESPHome `sensor.adc` + calibration; no profiling yet |
| Flow meter | ✅ `espresso_machine_flow_meter` | Optional — requires physical installation |
| Shot profiles (Gaggiuino-compatible) | ⬜ Phase 12 | See `docs/profiles.md` |
| Time-based shot termination | ⬜ Minor schema change | Need `brew_timeout` key or fallback when `flow_max` = 0 |

### 5.5  New tasks needed for GaggiaMate support

- [ ] Document GaggiaMate pinout in `docs/wiring.md` (new section)
- [ ] Create `examples/gaggiamate.yaml` — reference config for GaggiaMate PCB
- [ ] Allow `brew: valve:` and `brew: purge_valve:` to share the same `id:` (currently
      schema may reject duplicate references — validate and fix if needed)
- [ ] Add `brew_timeout:` key to `brew:` sub-schema (time-based shot termination)
- [ ] Make `flow_max` / `flow_offset` optional with defaults (`999ml` / `0ml`)
- [ ] Document NTC calibration procedure for common Gaggia boiler NTCs

---

## 6  Dual Thermoblock Machines

### 6.1  Hardware topology

```
Brew thermoblock        Steam thermoblock
  ├── SSR 1               ├── SSR 2
  └── NTC / TC sensor 1  └── NTC / TC sensor 2
```

Both thermoblocks share the same pump and valve manifold but have **independent
temperature control**.  Steam is always ready — no need to wait for a temperature ramp.

### 6.2  What works today (no new code)

The existing orchestrator already supports independent `heater_controller` references
for brew and steam — but they both reference the **same entity** in the current schema.
For dual thermoblock, we need each sub-schema to reference a **different** entity.

```yaml
# Two independent heater controllers
espresso_machine_heater:
  - id: brew_heater_ctrl
    climate_id: brew_climate
  - id: steam_heater_ctrl
    climate_id: steam_climate

espresso_machine:
  id: my_espresso
  brew:
    heater: brew_climate
    heater_controller: brew_heater_ctrl    # brew thermoblock only
    ...
  steam:
    heater: steam_climate
    heater_controller: steam_heater_ctrl   # steam thermoblock only
    ...
```

This YAML is valid if `espresso_machine_heater` supports `MULTI_CONF: True`.
The orchestrator already resolves brew and steam heater controllers independently.

### 6.3  Required changes

- [ ] Add `MULTI_CONF = True` to `espresso_machine_heater/__init__.py` (one-line change)
- [ ] Verify orchestrator correctly uses `brew_heater_ctrl_` and `steam_heater_ctrl_`
      independently (already separated in C++)
- [ ] Create `examples/dual_thermoblock.yaml` — reference config
- [ ] Document dual thermoblock setup in `docs/wiring.md`

---

## 7  Dual Boiler Machines

### 7.1  Hardware topology

```
Brew boiler (200–300 mL)       Steam boiler (500–900 mL)
  ├── SSR 1                      ├── SSR 2
  ├── NTC / TC sensor 1          ├── NTC / TC sensor 2
  └── Brew group + valve         └── Steam wand + valve
```

Key differences from dual thermoblock:
- **Much larger thermal mass** — slower temperature response; PID tuning differs significantly.
- **Both boilers are always powered** — steam is always ready; brew boiler stays at brew temp.
- **Separate water paths** — brew boiler feeds the group head; steam boiler feeds the wand.
- **Heat exchanger (HX) variant** — single boiler machine where brew water is heated by passing
  through a coil inside the steam boiler; a distinct topology (see §7.4).
- **No temperature transition needed** — brew and steam can occur simultaneously.

### 7.2  YAML (using existing components — no new code)

```yaml
# Dual boiler — two independent heaters, shared pump
espresso_machine_heater:
  - id: brew_boiler_ctrl
    climate_id: brew_boiler_pid
    temperature_tolerance: 1.0    # larger boiler — wider tolerance acceptable

  - id: steam_boiler_ctrl
    climate_id: steam_boiler_pid
    temperature_tolerance: 2.0    # steam boiler — less precision needed

espresso_machine_valve:
  - id: brew_valve
    pin: GPIO26
  - id: steam_valve
    pin: GPIO27
  - id: purge_valve
    pin: GPIO14

espresso_machine_pump:
  id: main_pump
  type: relay
  pin: GPIO25

espresso_machine:
  id: my_espresso
  brew:
    heater: brew_boiler_pid
    heater_controller: brew_boiler_ctrl
    pump: main_pump
    valve: brew_valve
    purge_valve: purge_valve
    target_temperature: 93°C
    flow_max: 40ml
    flow_offset: 20ml
  steam:
    heater: steam_boiler_pid     # ← different heater entity
    heater_controller: steam_boiler_ctrl
    pump: main_pump
    valve: steam_valve
    purge_valve: purge_valve
    target_temperature: 130°C
    flow_max: 2ml/s
    cool_down_to: 93°C           # cool_down_to ignored if steam boiler is always on
    timeout: 5min
```

### 7.3  Required changes

Same as §6.3 — primarily `MULTI_CONF = True` on `espresso_machine_heater`, plus:

- [ ] `steam: cool_down_to:` should be optional (omit to disable the COOLING→CLEANUP
      temperature gate for dual-boiler setups where the steam boiler is never powered
      down; the CLEANUP state transitions immediately without waiting for temperature).
- [ ] Create `examples/dual_boiler.yaml` — reference config
- [ ] Document dual boiler PID tuning notes in `docs/pid_tuning.md`

### 7.4  Heat Exchanger (HX) machines

An HX machine (e.g., Rocket Espresso Giotto, Bezzera BZ10) has a single large steam
boiler and a **coil of copper tube** inside it.  Cold brew water is pumped through the
coil, heated by contact with the boiler water, and exits at brew temperature.

From a control perspective this is similar to **single boiler** — one SSR, one
temperature sensor.  The brew temperature is set lower (steam boiler at ~125 °C; brew
water exits the coil at ~92–96 °C depending on flow rate and thermosyphon effect).

HX machines work with the **existing** orchestrator and a single `espresso_machine_heater`.
The only difference is PID parameter tuning (much larger thermal mass and longer lag time).

---

## 8  Dual NTC, Single Thermoblock

### 8.1  Hardware topology

Two NTC probes at different physical positions on the same thermoblock:

```
NTC probe 1 — near the heating element (upstream temperature)
NTC probe 2 — near the group head / output (downstream temperature)

Purpose:
  - Better characterise the thermal gradient across the block
  - Enable the 3-node thermal distance model already in espresso_machine_mock_heater
  - Drive temperature surfing more accurately (NTC2 tracks water-side temp)
```

### 8.2  What works today (no new code)

Two standard ESPHome `ntc` (or `max6675`) sensors can be declared independently.
The `espresso_machine_heater` references only one (the PID control sensor).

```yaml
sensor:
  - platform: ntc
    id: thermoblock_heater_side   # upstream — near element
    ...
  - platform: ntc
    id: thermoblock_water_side    # downstream — near group head
    ...

climate:
  - platform: pid
    id: main_pid
    sensor: thermoblock_heater_side   # PID driven by upstream sensor

espresso_machine_heater:
  id: heater_ctrl
  climate_id: main_pid
```

The downstream sensor (`thermoblock_water_side`) is published as a separate HA entity
and can be used for:
- Display of actual water temperature at the group head
- Temperature surfing setpoint adjustment (via `on_value` ESPHome automation)
- Future: dual-sensor weighted average as the PID input

### 8.3  Required changes

- [ ] `espresso_machine_heater:` — add optional `secondary_sensor:` key that references
      a second temperature sensor; published as an additional HA sensor entity.
- [ ] Orchestrator `IHeater` interface — add `get_secondary_temperature()` virtual method
      (default returns `NAN`) so `BrewController` can log both temperatures.
- [ ] Update `docs/pid_tuning.md` — section on dual-NTC PID strategy.
- [ ] Create `examples/dual_ntc_single_thermoblock.yaml` — reference config.

---

## 9  Dual Pump, Single Thermoblock (Cold Water Injection)

### 9.1  Concept

A second small pump injects **cold water** directly into the thermoblock water path during
cool-down, dramatically accelerating the temperature drop between steam and brew modes.

```
Normal pump (GPIO25) ──► Group head (brew)
                     └──► Steam wand (steam)

Cooling pump (GPIO28) ──► Cold water input ──► Thermoblock inlet
                          (from fresh water tank, before the heat path)
```

This eliminates the need for the slow passive cool-down after steaming: instead of waiting
2–4 minutes for the thermoblock to cool naturally, the cooling pump runs for ~30 seconds
and the machine is brew-ready.

### 9.2  What works today (no new code)

The cooling pump can be wired as a second `espresso_machine_pump` entity.  The cool-down
can be triggered via an ESPHome automation that watches the machine status text sensor:

```yaml
espresso_machine_pump:
  - id: main_pump
    type: relay
    pin: GPIO25

  - id: cooling_pump
    type: relay
    pin: GPIO28

automation:
  - trigger:
      platform: text_sensor
      entity_id: text_sensor.my_espresso_status
      to: "Cooling"
    then:
      - switch.turn_on: cooling_pump
  - trigger:
      platform: text_sensor
      entity_id: text_sensor.my_espresso_status
      to: "Idle"
    then:
      - switch.turn_off: cooling_pump
```

### 9.3  Required changes (first-class support)

To make the cooling pump a first-class orchestrator feature rather than an automation hack:

- [ ] Add optional `cooling_pump:` key to the `espresso_machine:` top-level schema
      (or to the `brew:` sub-schema, since cool-down is relevant to pre-brew cooling).
- [ ] Orchestrator `COOLING` brew state: if `cooling_pump_` is wired, call
      `cooling_pump_->turn_on()` on entry and `cooling_pump_->turn_off()` on exit.
- [ ] `espresso_machine_pump` must support `MULTI_CONF = True` (already set).
- [ ] Create `examples/dual_pump_cooling.yaml` — reference config.
- [ ] Document cooling pump wiring in `docs/wiring.md`.

---

## 10  New Components Required

### 10.1  Summary table

| Component | Machine types | Priority | Effort |
|---|---|---|---|
| `MULTI_CONF` on `espresso_machine_heater` | Dual thermoblock, dual boiler | **High** | Trivial (1 line) |
| Optional `brew_timeout:` in brew schema | Minimal / GaggiaMate | **High** | Low |
| Optional `flow_max` / `flow_offset` defaults | Minimal / GaggiaMate | **High** | Low |
| Allow shared `valve:` + `purge_valve:` id | GaggiaMate / single-boiler | **High** | Low |
| Optional `cooling_pump:` in orchestrator | Dual pump / cold injection | Medium | Medium |
| Optional `secondary_sensor:` on `espresso_machine_heater` | Dual NTC | Medium | Medium |
| Optional `cool_down_to:` in steam schema | Dual boiler | Medium | Low |
| `espresso_machine_pressure:` platform | Dual boiler / profiling | Low | High |

### 10.2  `espresso_machine_pressure` (future)

A new optional platform that wraps a 0–16 bar pressure transducer (e.g., GEMS 2200,
Honeywell MLH), publishes pressure as a HA sensor entity, and exposes `IPressure`
interface to the orchestrator for pressure-based brew phase transitions.

```yaml
espresso_machine_pressure:
  id: brew_pressure
  pin: GPIO35
  min_bar: 0.0
  max_bar: 16.0
  adc_min_v: 0.5
  adc_max_v: 4.5
  sensor:
    name: "Brew Pressure"
    unit_of_measurement: bar
    accuracy_decimals: 1
```

This is a prerequisite for pressure profiling (Phase 12+) and is tracked separately.

---

## 11  Orchestrator Extensions Required

### 11.1  Schema changes

| Change | Reason | Breaking |
|---|---|---|
| `brew: flow_max:` — make optional (default 999 ml) | No flow meter support | No |
| `brew: flow_offset:` — make optional (default 0 ml) | No flow meter support | No |
| `brew: valve:` — make optional | Minimalist builds | No |
| `brew: brew_timeout:` — new optional key | Time-based shot termination | No |
| `steam: cool_down_to:` — make optional | Dual boiler (always hot) | No |
| `espresso_machine: cooling_pump:` — new optional key | Cold water injection | No |
| Allow `brew: valve:` and `brew: purge_valve:` to share same id | Single solenoid machines | No |

### 11.2  C++ changes

| Change | File | Effort |
|---|---|---|
| `brew_timeout_ms_` field + timer check in `BREWING` tick | `espresso_machine.cpp` | Low |
| Null-check before using `brew_valve_` in BrewController | `espresso_machine.cpp` | Low |
| `cooling_pump_` pointer + activation in `COOLING` state | `espresso_machine.cpp` | Medium |
| `IHeater::get_secondary_temperature()` default impl | `interfaces.h` | Low |
| Dual-id validation: warn if `brew_valve_` and `brew_purge_valve_` point to same object | `espresso_machine.cpp` | Low |

---

## 12  Component Reuse Summary

### 12.1  Components reusable as-is (zero changes)

| Component | All machine types? | Notes |
|---|---|---|
| `espresso_machine_flow_meter` | Yes — optional | `MULTI_CONF` already set |
| `espresso_machine_valve` | Yes — optional | `MULTI_CONF` already set |
| `espresso_machine_pump` | Yes — optional | `MULTI_CONF` already set |
| `espresso_machine_grinder` | Yes — optional | `MULTI_CONF` already set |
| Native ESPHome `climate.pid` | Yes | Any number of instances |
| Native ESPHome `sensor.ntc` / `max6675` | Yes | Any number of instances |
| Native ESPHome `output.slow_pwm` | Yes | Any number of instances |
| `espresso_machine_sprofiler` | Yes | Shot logging; machine-type agnostic |

### 12.2  Components needing minor changes

| Component | Change needed | Effort |
|---|---|---|
| `espresso_machine_heater` | Add `MULTI_CONF = True`; add `secondary_sensor:` key | Trivial + Low |
| `espresso_machine` orchestrator | See §11 | Low–Medium total |

### 12.3  New components (not yet written)

| Component | Purpose | Needed for |
|---|---|---|
| `espresso_machine_pressure` | Pressure transducer platform | Pressure profiling (future) |

---

## 13  Phased Implementation Plan

### Phase A — Baseline Decoupling (minimum-viable machine support)

> **Goal:** Any machine can be controlled with zero sensors and zero flow meter.

Tasks:
- [ ] Make `brew: flow_max:` optional (default `999ml`)
- [ ] Make `brew: flow_offset:` optional (default `0ml`)
- [ ] Make `brew: valve:` optional
- [ ] Add `brew: brew_timeout:` optional key (ms; 0 = disabled)
- [ ] Allow `brew: valve:` and `brew: purge_valve:` to reference the same `id:`
- [ ] Make `steam: cool_down_to:` optional (default: current value required)
- [ ] Add `MULTI_CONF = True` to `espresso_machine_heater/__init__.py`
- [ ] Update C++ for null-checks on optional pointers
- [ ] Unit tests: brew with no valve, brew with no flow meter, brew timeout
- [ ] Create `examples/minimal.yaml` — no sensors, relay + timer only
- [ ] Update `docs/wiring.md` — minimal build section

### Phase B — GaggiaMate Example & Validation

> **Goal:** Working, tested ESPHome firmware for GaggiaMate PCB.

Tasks:
- [ ] Create `examples/gaggiamate.yaml` — complete config for GaggiaMate PCB
- [ ] Document GaggiaMate pinout in `docs/wiring.md`
- [ ] Validate: `esphome config examples/gaggiamate.yaml` passes in CI
- [ ] Write `tests/test_gaggiamate.yaml` for CI compilation check (host target)
- [ ] NTC calibration guide for Gaggia Classic boiler in `docs/pid_tuning.md`

### Phase C — Single Boiler Example

> **Goal:** Complete reference config for Gaggia Classic / Rancilio Silvia style machines.

Tasks:
- [ ] Create `examples/single_boiler.yaml`
- [ ] Document shared valve / pressurestat bypass patterns in `docs/wiring.md`
- [ ] Validate in CI

### Phase D — Dual Thermoblock & Dual Boiler Examples

> **Goal:** Complete reference configs for dual-element machines.

Tasks:
- [ ] Create `examples/dual_thermoblock.yaml`
- [ ] Create `examples/dual_boiler.yaml`
- [ ] Validate `MULTI_CONF = True` on heater enables multiple heater instances
- [ ] Add dual-boiler PID tuning notes to `docs/pid_tuning.md`
- [ ] Validate in CI

### Phase E — Dual NTC & Dual Pump Examples

Tasks:
- [ ] `espresso_machine_heater:` — add `secondary_sensor:` optional key
- [ ] Add `IHeater::get_secondary_temperature()` default in `interfaces.h`
- [ ] Add optional `cooling_pump:` to orchestrator schema and C++
- [ ] Create `examples/dual_ntc.yaml`
- [ ] Create `examples/dual_pump_cooling.yaml`
- [ ] Unit tests for cooling pump activation in COOLING state
- [ ] Validate in CI

### Phase F — Pressure Transducer Platform (Future)

> **Depends on Phase 12 (brew profiles) for pressure-based exit conditions.**

Tasks:
- [ ] Create `components/espresso_machine_pressure/__init__.py`
- [ ] C++ `IPressure` interface in `interfaces.h`
- [ ] Wire `IPressure` into brew state machine for pressure-based phase transitions
- [ ] Create `examples/with_pressure_transducer.yaml`
- [ ] Document transducer wiring and calibration in `docs/wiring.md`

---

## 14  Open Questions

1. **Pressurestat bypass wiring:** For Gaggia Classic, bypassing the pressurestat requires
   cutting a wire and inserting an SSR.  Should the wiring guide document a "reversible"
   bypass (add SSR in series, pressurestat still active as a backup) vs. a "full bypass"
   (remove pressurestat completely)?

2. **GaggiaMate OLED + rotary encoder:** GaggiaMate's firmware has a local UI (OLED menu
   driven by rotary encoder).  ESPHome's `display` lambda can replicate a simpler version.
   Should we maintain a reference OLED layout for GaggiaMate (showing temp, mode, setpoint)?

3. **HX machine flow-through temperature model:** On an HX machine, the brew temperature
   depends on the thermosyphon effect and how long since the last shot.  A pre-infusion
   "temperature stabilisation flush" (a short burst of water before the shot) is standard
   practice.  Should the `brew:` schema have a `stabilise_flush_volume:` key for this?

4. **Dual boiler — simultaneous brew and steam:** The current orchestrator enforces mutual
   exclusion (brew and steam cannot run simultaneously).  For a true dual-boiler machine,
   this restriction should be lifted — each heater is independent and simultaneous operation
   is safe.  A `dual_boiler: true` flag (or detecting that `brew_heater_ctrl_` ≠
   `steam_heater_ctrl_`) could remove the mutual exclusion check.

5. **GaggiaMate PCB rev compatibility:** GaggiaMate has had multiple PCB revisions with
   different GPIO assignments.  Should we maintain a separate YAML example per PCB revision,
   or use a single YAML with clearly-labelled substitution variables?

---

## Related Documents

| Document | Relevance |
|---|---|
| [`docs/esphome_redesign.md`](esphome_redesign.md) | Decoupling proposals (Proposals A, C, E) that enable multi-machine support |
| [`docs/profiles.md`](profiles.md) | Brew profiles — multi-phase curves apply to all machine types |
| [`docs/scales.md`](scales.md) | Scale platform — weight-based exit applies to all machine types |
| [`docs/wiring.md`](wiring.md) | Current wiring guide (Philips Barista Brew only — will be extended) |
| [`docs/pid_tuning.md`](pid_tuning.md) | PID guide — will be extended with boiler-specific tuning |
| [`PLAN.md`](../PLAN.md) | Phased roadmap — Phases A–F above will be added here |
| [`FEATURES.md`](../FEATURES.md) | Feature status — update when Phase A tasks complete |
| [`examples/philips_barista_brew.yaml`](../examples/philips_barista_brew.yaml) | Current reference (single thermoblock) |
