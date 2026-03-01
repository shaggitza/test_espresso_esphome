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
9. [Dual Pump, Single Thermoblock — Active Temperature Profiling via Cool-Water Injection](#9-dual-pump-single-thermoblock--active-temperature-profiling-via-cool-water-injection)
10. [Boiler with Water Inlet Pre-Heater](#10-boiler-with-water-inlet-pre-heater)
11. [Heat Exchanger (HX) Machines — E61 Group Head and Thermosyphon](#11-heat-exchanger-hx-machines--e61-group-head-and-thermosyphon)
12. [Variable-Speed Rotary Pump — Pressure and Flow Profiling](#12-variable-speed-rotary-pump--pressure-and-flow-profiling)
13. [Advanced Commercial, Lever, and Edge-Case Configurations](#13-advanced-commercial-lever-and-edge-case-configurations)
14. [New Components Required](#14-new-components-required)
15. [Orchestrator Extensions Required](#15-orchestrator-extensions-required)
16. [Component Reuse Summary](#16-component-reuse-summary)
17. [Phased Implementation Plan](#17-phased-implementation-plan)
18. [Open Questions](#18-open-questions)

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
(pressurestat controls heat)                              + inlet pre-heater
(time-based shot only)                                    + flow meter
                                                          + variable-speed rotary pump
                                                          + dual cool-water pump
                                                          + pressure transducer
                                                          + scale
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
| **Dual pump, single thermoblock** | Custom Decent-style mod | 1 × thermoblock + SSR | 1 × sensor (+ optional 2nd for group inlet) | 1 × AB32 | Optional | 2 × vibration (brew + cool) |
| **Dual thermoblock** | DeLonghi Dinamica Pro | 2 × thermoblock + 2 × SSR | 2 × sensors | Optional | Optional | 1 × vibration |
| **Dual boiler** | Breville Dual Boiler, ECM Synchronika, Profitec Pro 700 | 2 × boiler + 2 × SSR | 2 × sensors | Optional | None | 1 × vibration |
| **With inlet pre-heater** | Any thermoblock/boiler machine + secondary element | 1–2 × primary + 1 × inlet heater | 1–2 × primary + 1 × inlet NTC | Optional | Optional | 1 × vibration |
| **HX / E61** | Rocket Appartamento, ECM Mechanika, Quick Mill, Bezzera, Lelit Bianca | 1 × steam boiler (HX coil inside) | 1 × NTC on boiler | Optional | None | 1 × vibration or rotary |
| **Variable-speed rotary pump** | La Marzocca Strada EP, Slayer, Decent DE1, VA Black Eagle | 1–2 × boiler/thermoblock | 1–2 × sensors | Optional | Optional | 1 × variable-speed rotary |
| **Multi-group commercial** | La Marzocca Linea, Dalla Corte, Nuova Simonelli Aurelia | 1–2 × large boiler | 1–2 × sensors | 1 per group (optional) | None | 1 rotary (shared) or 1 per group |

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

## 9  Dual Pump, Single Thermoblock — Active Temperature Profiling via Cool-Water Injection

### 9.1  Concept and Inspiration

This technique is inspired by the **Decent DE1** and similar lever-influenced machines.
The core idea: instead of changing the thermoblock setpoint and waiting for it to
respond (slow, limited by thermal mass), a **second small pump injects cool water from
the reservoir directly into the group head water path** in real time.  By controlling
the ratio of hot water (thermoblock output) to cool water (direct injection), the
system can achieve **rapid, precise temperature changes at the puck** that would be
physically impossible through heater control alone.

```
Water tank
    │
    ├──► Main pump (GPIO25) ──► Thermoblock ──► Brew valve ──► Group head ──► Puck
    │
    └──► Cool pump  (GPIO28) ─────────────────────────────────► Group head (mixing tee)
                              bypasses thermoblock entirely
```

The cool pump draws **unheated water** from the same reservoir and injects it
downstream of the thermoblock, typically via a mixing tee or a Y-fitting just before
the brew valve or group head inlet.  Both pumps run simultaneously; the cool pump's
duty cycle or speed controls how much cool water is blended in.

### 9.2  What this enables

#### 9.2.1  Temperature descent profiles (Decent-style)

The Decent DE1's signature "temperature descent" profile works exactly this way:
extraction starts at a high temperature (e.g. 95 °C) and temperature ramps down
smoothly (e.g. to 88 °C by the end of the shot).  This is thought to improve
extraction uniformity — higher temperature dissolves solubles early in the shot when
the puck is dense; lower temperature avoids over-extraction at the end when the puck
is more permeable.

With dual pumps and a thermoblock, the orchestrator can:
1. Set the thermoblock to a fixed high setpoint (e.g. 96–98 °C) — it never changes.
2. Ramp the cool pump duty cycle up over the shot to add progressively more cool water.
3. The actual water temperature at the puck follows the blending ratio, not the
   thermoblock temperature.

This decouples "fast response" (pump blending ratio, near-instant) from "coarse
setpoint" (thermoblock PID, slow but stable).

#### 9.2.2  Temperature ascent profiles

The inverse: thermoblock at a lower setpoint, cool pump off at the start, then
increasing the main pump speed (or reducing main pump slightly) to shift the ratio.
Less commonly needed but architecturally identical.

#### 9.2.3  Rapid post-steam cool-down (secondary benefit)

Running the cool pump for 20–30 seconds through the group head after a steam session
flushes residual hot water and rapidly lowers the thermoblock effective temperature,
cutting steam-to-brew turnaround from 2–4 minutes to under a minute.  This is a
secondary benefit of the same hardware — the primary value is in-shot temperature
profiling.

### 9.3  Hardware requirements

| Component | Notes |
|---|---|
| Main vibration pump | Standard — drives water through thermoblock |
| Second vibration pump | Same or smaller model; 3 bar is enough for blending |
| Mixing tee or Y-fitting | Stainless or food-grade brass; installed just before brew valve |
| Check valve on cool path | Prevents back-flow of hot water into the cool pump when cool pump is off |
| Check valve on hot path | Recommended; prevents pressure equalisation through thermoblock |

> ⚠️ **Pressure balance:** Both pumps work against the same downstream pressure (the
> puck bed resistance, typically 8–9 bar).  A vibration pump's flow rate drops with
> back-pressure.  At 9 bar, a standard 60 W pump delivers approximately 60–80 mL/min.
> Without check valves, the higher-pressure pump will push water backward through the
> other pump, so check valves are mandatory.

### 9.4  Temperature model

The resulting brew temperature at the puck is approximately:

```
T_puck ≈ (Q_hot × T_hot + Q_cool × T_cool) / (Q_hot + Q_cool)

Where:
  Q_hot   = main pump flow rate (mL/s)  — controlled by main pump
  T_hot   = thermoblock output temperature — set by PID, ~96-98 °C
  Q_cool  = cool pump flow rate (mL/s)   — controlled by cool pump
  T_cool  = reservoir water temperature  — ambient, typically 15-22 °C
```

**Example:** If Q_hot = 2 mL/s, T_hot = 97 °C, Q_cool = 0.5 mL/s, T_cool = 18 °C,
then T_puck ≈ (2 × 97 + 0.5 × 18) / 2.5 ≈ 81 °C.

> **Important:** The actual temperature also depends on heat losses in the brew path,
> group head thermal mass, and puck temperature.  Real-world calibration is required.
> A downstream temperature sensor (NTC or TC near the group head inlet) is strongly
> recommended for closed-loop control (see §8, Dual NTC).

### 9.5  Control strategies

#### Strategy A — Open-loop cool pump duty cycle (simple)

A fixed, pre-programmed duty cycle ramp drives the cool pump.
No feedback — the profile is purely a time-based ramp.

```
Time:   0s ──────────────── 25s ──────────────── 50s
Q_cool: 0  ────────────────  →  ────────────────  0.8 mL/s
T_puck: 93°C                 →                   88°C (approx)
```

This is the simplest implementation and requires no new sensors.

#### Strategy B — Closed-loop with downstream NTC (recommended)

If a second NTC sensor is placed at the group head inlet (see §8, Dual NTC), a PID
loop can drive the cool pump to hit a target temperature at the puck rather than a
fixed duty cycle.  This compensates for reservoir temperature changes across seasons.

#### Strategy C — Coupled heater + cool pump (full profiling)

The thermoblock PID tracks a moving setpoint (as in temperature surfing, see
`FEATURES.md`), and the cool pump provides fast correction.  The combination can
follow steep temperature descent ramps (e.g., −10 °C over 10 seconds) that neither
the heater nor the cool pump could achieve alone.

### 9.6  What works today (no new code for basic automation use)

The cool pump can be declared as a second `espresso_machine_pump` entity and driven
via ESPHome automations from HA:

```yaml
espresso_machine_pump:
  - id: main_pump
    type: relay
    pin: GPIO25

  - id: cool_pump
    type: relay          # or 'dimmer' if using a TRIAC dimmer for flow rate control
    pin: GPIO28

# Example HA automation: temperature descent — ramp cool pump during brew
# (replace with actual ESPHome script/automation as needed)
script:
  - id: temp_descent_profile
    then:
      - delay: 10s           # first 10 s: brew at full thermoblock temp
      - switch.turn_on: cool_pump
      - delay: 20s           # next 20 s: cool injection active
      - switch.turn_off: cool_pump

# Tie the script into the brew start action
on_brew_start:          # Phase 11+ hook — not yet implemented; use HA automation today
  then:
    - script.execute: temp_descent_profile
```

### 9.7  Required changes for first-class orchestrator support

#### Schema additions

```yaml
espresso_machine:
  id: my_espresso
  brew:
    pump: main_pump
    cool_pump: cool_pump               # NEW — optional second pump for cool water injection
    cool_pump_profile:                 # NEW — time-based cool pump ramp during shot
      - at: 0s
        flow_fraction: 0.0             # fraction of cool pump max flow (0.0–1.0)
      - at: 10s
        flow_fraction: 0.0             # full thermoblock temp for first 10 s
      - at: 30s
        flow_fraction: 0.25            # 25% cool water blend by 30 s
      - at: 50s
        flow_fraction: 0.40            # 40% by end of shot
    ...
```

#### Task checklist

- [ ] Add optional `cool_pump:` key to `brew:` sub-schema (references an
      `espresso_machine_pump` entity)
- [ ] Add optional `cool_pump_profile:` key — a list of `{at: <time>, flow_fraction: <float>}`
      entries that the orchestrator interpolates during the `BREWING` state
- [ ] Orchestrator `BREWING` tick: if `cool_pump_` is set, compute the interpolated
      `flow_fraction` for the current elapsed time and call `cool_pump_->set_target_flow()`
      (using the pump's existing bang-bang flow rate interface)
- [ ] Orchestrator `COOLING` state (post-steam): if `cool_pump_` is set, run it at
      full duty until temperature drops to `cool_down_to:` (secondary benefit)
- [ ] Add `ICoolPump` interface (or reuse `IPump` with `set_target_flow()`) — verify
      the existing `IPump` interface covers this use case
- [ ] `espresso_machine_pump` must support `MULTI_CONF = True` (already set)
- [ ] Unit tests: profile interpolation at t=0, t=mid, t=end; cool pump on/off in
      BREWING and COOLING states
- [ ] Create `examples/dual_pump_temp_descent.yaml` — reference config with annotated
      temperature descent profile
- [ ] Document mixing tee wiring, check valve placement, and flow calibration in
      `docs/wiring.md`
- [ ] Document temperature model and calibration procedure in `docs/pid_tuning.md`

### 9.8  Relationship to brew profiles (Phase 12)

The `cool_pump_profile:` key described above is a simplified per-key temperature
profile.  Once the full brew profile system (Phase 12, see `docs/esphome_redesign.md`
Proposal C) is implemented, the cool pump ramp will be expressed as a phase in the
profile rather than a separate flat key.  The flat `cool_pump_profile:` is a
forward-compatible staging step.

### 9.9  Example: Decent-style temperature descent

```yaml
espresso_machine:
  id: my_espresso
  brew:
    pump: main_pump
    cool_pump: cool_pump
    target_temperature: 97°C     # thermoblock held high and fixed
    cool_pump_profile:
      - at: 0s
        flow_fraction: 0.0       # 0–10 s: pure thermoblock water ~97°C
      - at: 10s
        flow_fraction: 0.0
      - at: 15s
        flow_fraction: 0.10      # blend starts at 15 s → ~90°C at puck (calibrate!)
      - at: 25s
        flow_fraction: 0.18      # continuing descent → ~87°C
      - at: 40s
        flow_fraction: 0.22      # levelling off → ~85°C
    flow_max: 40ml
    flow_offset: 20ml
    valve: brew_valve
    purge_valve: purge_valve
    heater: main_heater
    heater_controller: heater_ctrl
```

> **Calibration note:** `flow_fraction` values depend on pump characteristics, check
> valve cracking pressure, brew path resistance, and ambient water temperature.
> Always measure the actual puck temperature with a group head thermometer or
> thermofilter before relying on a profile in production.

---

## 10  Boiler with Water Inlet Pre-Heater

### 10.1  Concept and Motivation

The user's request: "a boiler with a pre-heater right behind it, to better regulate the
inlet water temperature and not cool the boiler too much, or the other way around."

An **inlet pre-heater** is a secondary heating element positioned in the water path
**between the reservoir/pump and the main boiler or thermoblock**.  Its purpose:

1. **Prevent thermal shock** — Cold water hitting the boiler causes rapid temperature
   drops and repeated thermal stress cycles on the boiler walls and seals.
2. **Maintain temperature stability** — Pre-heating inlet water to e.g. 60 °C means the
   main boiler only needs to raise water by ~35 °C instead of ~75 °C, dramatically
   reducing temperature variance between shots.
3. **Reduce heater recovery time** — Less energy required per shot; recovery faster
   for back-to-back extractions.

```
Water tank
    │
    ▼
 Main pump
    │
    ▼
┌─────────────────────┐
│  Inlet Pre-Heater   │  ← small SSR-controlled element; NTC monitors inlet temp
│  setpoint: ~60–70°C │    independent PID loop; runs whenever machine is on
└─────────────────────┘
    │
    ▼
┌─────────────────────┐
│  Main Thermoblock   │  ← receives ~65°C water instead of ~15°C tap water
│  setpoint: 93–98°C  │    PID only needs to raise Δ30°C instead of Δ80°C
└─────────────────────┘
    │
    ▼
 Group head → Puck
```

### 10.2  Commercial examples

| Manufacturer | Implementation |
|---|---|
| **Slayer Espresso** | Routes incoming cold water through a pre-heat jacket *around* the steam boiler, recovering waste heat before the water enters the brew boiler |
| **Victoria Arduino T3** | "Triple temperature control" — three independent PIDs: inlet water, brew group, steam |
| **Dalla Corte** | Per-group boiler systems often include inlet temp conditioning to ensure stable fill temps at high throughput |
| **Decent DE1** | Uses a multi-segment thermoblock; initial segments act as a pre-heater before final segments reach brew temp |

### 10.3  What works today (no new code)

Both the inlet heater and the main heater can already be declared as separate native
ESPHome `climate.pid` + `output.slow_pwm` blocks.  The inlet heater runs completely
independently — no orchestrator changes required for basic operation.

```yaml
# Inlet pre-heater (native ESPHome — no custom component needed)
sensor:
  - platform: ntc
    id: inlet_temp
    sensor: inlet_adc
    calibration:
      b_constant: 3950
      reference_temperature: 25°C
      reference_resistance: 10k
    name: "Inlet Water Temperature"
    ...

output:
  - platform: slow_pwm
    id: inlet_ssr
    pin: GPIO12
    period: 2s

climate:
  - platform: pid
    id: inlet_heater_pid
    name: "Inlet Pre-Heater"
    sensor: inlet_temp
    heat_output: inlet_ssr
    default_target_temperature: 65°C
    control:
      kp: 0.8
      ki: 0.003
      kd: 0.0

# The main thermoblock PID and espresso_machine: are declared as usual
# No changes to espresso_machine schema required for basic use
```

### 10.4  Required changes for first-class orchestrator support

For the orchestrator to coordinate the inlet heater as part of the machine lifecycle
(wait for inlet to reach temp before brew, keep it active in standby, etc.):

#### Schema additions

```yaml
espresso_machine:
  id: my_espresso
  brew:
    heater: main_heater
    inlet_heater: inlet_heater_pid      # NEW — optional reference to inlet heater
    inlet_target_temperature: 65°C      # NEW — inlet setpoint (default: no constraint)
    ...
```

#### Task checklist

- [ ] Add optional `brew: inlet_heater:` key — references a native `climate` entity
- [ ] Add optional `brew: inlet_target_temperature:` key — if set, orchestrator waits for
      inlet to reach this temperature before entering `BREWING` state
- [ ] Orchestrator `HEATING` state: if `inlet_heater_` is set, check inlet temperature
      in addition to main heater temperature before allowing brew
- [ ] Orchestrator `IDLE` state: if `inlet_heater_` is set, keep it enabled (always-ready)
- [ ] Add `IInletHeater` interface or reuse `IHeater::get_current_temperature()` (the
      inlet heater is just another climate entity — no new interface needed)
- [ ] Create `examples/with_inlet_preheater.yaml` — reference config
- [ ] Document inlet pre-heater wiring and NTC placement in `docs/wiring.md`
- [ ] Add inlet pre-heater section to `docs/pid_tuning.md` — tuning is simpler than
      brew PID (no brewing disturbance, much lower setpoint gradient needed)

### 10.5  Interaction with other machine types

| Combined with | Notes |
|---|---|
| Dual thermoblock (§6) | Both the inlet heater and the brew thermoblock receive independent PIDs — fully compatible |
| Dual boiler (§7) | Inlet pre-heater feeds both the brew boiler and the steam boiler fill path — single inlet heater shared |
| Dual NTC §8 | The inlet NTC can be the "upstream" sensor in the dual-NTC model, further improving thermal profiling accuracy |
| Dual pump / cool water §9 | The cool pump bypasses the inlet heater (cool water must remain cool) — the mixing tee is downstream of the thermoblock output only |
| HX machine §11 | Inlet pre-heater particularly valuable here — cold fill water destabilises HX brew temp; pre-heating to 50–60°C before entering the steam boiler coil improves shot-to-shot consistency significantly |

---

## 11  Heat Exchanger (HX) Machines — E61 Group Head and Thermosyphon

### 11.1  What is an HX machine?

A **heat exchanger (HX) machine** has a **single large boiler held at steam temperature**
(~125–135 °C).  Brew water is not drawn from the boiler directly; instead, it passes
through a copper or stainless coil **running inside the steam boiler**, absorbing heat as
it flows toward the group head.  The brew water never mixes with the steam water.

```
Water tank
    │
    ▼
 Main pump
    │
    ▼  ┌─────────────────────────────────┐
    └──►│     Steam Boiler (~130°C)       │
       │  ┌────────────────────────────┐ │──► Steam wand
       │  │  HX Coil (brew water path) │ │
       │  └─────────────┬──────────────┘ │
       └────────────────┼────────────────┘
                        │
                        ▼
                   E61 Group Head (kept warm by thermosyphon loop from boiler)
                        │
                        ▼
                     Puck
```

The **E61 group head** uses a **thermosyphon loop** — hot water from the boiler
circulates passively through the group head body by convection, keeping the brass group
at ~90–95 °C without any pump or active control.

**Commercial examples:** Rocket Appartamento, ECM Synchronika, Quick Mill Vetrano,
Bezzera Magica, Lelit Bianca, Profitec Pro series, Jura Giga line (commercial).

### 11.2  Key characteristics and challenges

| Characteristic | Impact on ESPHome control |
|---|---|
| Boiler runs at ~130°C | Heater PID setpoint is for steam, not brew |
| Brew temp depends on HX coil geometry + flow rate | Cannot be directly set; must be calibrated |
| **Temperature surfing required** | If machine is idle >5 min, HX water overheats → must flush 5–15 s before shot |
| Thermosyphon loop keeps group hot | No active group heater control needed |
| Brew and steam share the same boiler | Simultaneous brew + steam is possible but brew temp affected by steaming activity |
| NTC on boiler only | No direct brew temperature measurement without additional probe at group head inlet |

### 11.3  Temperature surfing — the key challenge

When the machine is idle, hot boiler water slowly heats the water sitting in the HX
coil beyond the target brew temperature.  A standard practice is to flush 5–10 seconds
of water through the group head just before brewing, cooling the HX coil back to
target brew temperature.

Without this flush, shots can be 5–15 °C hotter than intended, causing over-extraction.

```
Machine idle for >5 min:  HX coil water at ~103°C
After 5s flush:           HX coil back to ~93°C (stable zone)
Shot extraction begins:   temperature holds stable for the duration
```

### 11.4  What works today (no new code)

An HX machine uses the same `espresso_machine:` schema as a single boiler machine, with:
- `brew: heater:` and `steam: heater:` both referencing the **same** climate entity
- Steam setpoint (~130°C) is the operating setpoint; brew setpoint is also ~130°C
  (the brew group receives its temperature from the HX coil, not from PID control)
- The `purge_valve:` or `brew_valve:` can be opened briefly before a shot for the
  cooling flush, via an ESPHome automation or HA button press

### 11.5  Required changes for first-class HX support

- [ ] Add optional `brew: pre_flush_volume:` key — if set, orchestrator opens the brew
      valve and runs the pump for this volume (or `pre_flush_timeout:`) before entering
      `BREWING` state, then stops and pauses 5 s for temperature equilibration
- [ ] Add optional `brew: pre_flush_timeout:` key — time-based alternative to volume-based
      pre-flush for machines without a flow meter
- [ ] Optionally: `brew: hx_stabilise_delay:` — pause after flush before brew starts
      (allows the HX coil to re-equilibrate to steady state)
- [ ] New phase in brew state machine: `PRE_FLUSHING` → `FLUSH_PAUSE` → `HEATING` → `BREWING`
- [ ] Create `examples/hx_e61.yaml` — reference config for HX machines
- [ ] Add HX temperature surfing section to `docs/pid_tuning.md`

### 11.6  Interaction with the inlet pre-heater (§10)

On an HX machine, cold fill water enters the steam boiler to replenish what was used
for steam.  If the fill water is cold, the boiler temperature drops during fill, which
directly affects the HX coil temperature and therefore brew temperature.

An inlet pre-heater (§10) on an HX machine pre-heats fill water to 50–60 °C before
it enters the steam boiler, dramatically reducing temperature recovery time between
steam and brew.

---

## 12  Variable-Speed Rotary Pump — Pressure and Flow Profiling

### 12.1  Concept

A **variable-speed rotary pump** replaces or augments the standard vibration pump.
By controlling the pump speed (via PWM dimmer for AC motors, or a VFD/motor controller
for DC brushless), the system can dynamically vary **flow rate** and therefore
**back-pressure** at the puck during extraction.

This enables full **pressure profiling** (controlling bar at the puck) and **flow
profiling** (controlling mL/s through the puck) — the primary capability of machines
like the La Marzocca Strada EP, Slayer Espresso, and the Decent DE1.

```
Profile types enabled by variable-speed pump:

 Pressure (bar)
 │
 9 │     ┌──────────────────────────┐
   │    /                           \
 6 │   /                             └─────
   │  /  Pre-infusion → Ramp → Hold → Decline
 2 │──/
   └────────────────────────────────── time (s)
      0    5      15      30      45

 Flow (mL/s)
 │
 4 │         ┌─────────────┐
   │        /               \
 2 │       /                 \──────────
   │──────/
   └────────────────────────────────── time (s)
```

### 12.2  What already works

The existing `espresso_machine_pump` with `type: dimmer` (TRIAC-based PWM control)
already supports partial speed control of **vibration pumps**.  For vibration pumps,
speed control is limited (they are resonance-based), but partial duty does affect flow
rate.

For true **rotary pump** speed control via VFD (Variable Frequency Drive) or
DC brushless motor controller, the pump's PWM output maps directly to RPM, giving
precise, linear flow control.

### 12.3  Pump speed ↔ pressure relationship

| Pump duty | Flow rate (approx.) | Back-pressure at 9 bar |
|---|---|---|
| 100% | ~80 mL/min | ~80 mL/min (full flow) |
| 75% | ~55 mL/min | still ~9 bar (pump still reaches max pressure) |
| 50% | ~35 mL/min | ~5–6 bar (pump cannot reach 9 bar at this speed) |
| 25% | ~15 mL/min | ~2–3 bar (pre-infusion zone) |

> **Note:** Without a pressure transducer, there is no feedback on actual bar.
> Open-loop flow profiles are the baseline; closed-loop pressure profiles require
> adding an `espresso_machine_pressure` transducer (§15).

### 12.4  Required schema additions

```yaml
espresso_machine:
  id: my_espresso
  brew:
    pump: main_pump              # type: dimmer — supports variable speed
    pump_profile:                # NEW — time-based pump speed ramp during shot
      - at: 0s
        speed_fraction: 0.20    # ~2–3 bar pre-infusion
      - at: 8s
        speed_fraction: 0.20    # hold pre-infusion for 8 s
      - at: 12s
        speed_fraction: 1.0     # ramp to full pressure
      - at: 35s
        speed_fraction: 0.65    # declining pressure at end of shot
    ...
```

#### Task checklist

- [ ] Add optional `brew: pump_profile:` key — list of `{at: <time>, speed_fraction: <float>}`
      entries; orchestrator interpolates between them during `BREWING` state
- [ ] Orchestrator `BREWING` tick: if `pump_profile_` is set, compute interpolated
      `speed_fraction` at current elapsed time and call `pump_->set_speed(fraction)`
- [ ] This is architecturally identical to `cool_pump_profile:` — consider sharing the
      profile interpolation logic as a `BrewProfileInterpolator` utility class
- [ ] When both `pump_profile:` and `cool_pump_profile:` are set (§9), both ramps run
      simultaneously — the orchestrator drives two pumps independently
- [ ] Create `examples/rotary_pump_profiling.yaml` — reference config with a
      pre-infusion ramp → hold → declining pressure profile
- [ ] Unit tests: interpolation at boundary points; pump speed set correctly in BREWING
- [ ] Add section to `docs/pid_tuning.md` — pressure profiling calibration procedure

### 12.5  Closed-loop pressure profiling (future, requires §15)

When a pressure transducer is available, the `pump_profile:` keys can express target
**bar** values instead of `speed_fraction` values, and a pressure PID loop in the
orchestrator controls pump speed to hit the target:

```yaml
brew:
  pump_profile:
    - at: 0s
      target_bar: 2.0     # low pre-infusion
    - at: 10s
      target_bar: 9.0     # ramp to full brew pressure
    - at: 35s
      target_bar: 6.0     # declining pressure profile
```

This requires the `espresso_machine_pressure` platform (§14.2) to be implemented first.

---

## 13  Advanced Commercial, Lever, and Edge-Case Configurations

### 13.1  Multi-Group Commercial Machines

Commercial 2–3 group machines share a single boiler but have **one solenoid valve,
one group head, and one flow path per group**.  Each group can pull a shot
independently.

```
Boiler ──► Group 1 solenoid ──► Group 1 head
       └──► Group 2 solenoid ──► Group 2 head
       └──► Group 3 solenoid ──► Group 3 head

Shared: boiler heater, main pump (or one pump per group on some models)
```

**ESPHome support today:**
- `espresso_machine_valve` already has `MULTI_CONF = True` — each group valve is a
  separate entity
- The orchestrator could reference multiple valve instances, one per `brew:` block —
  but the current schema only supports a single `brew:` section

**Required changes:**
- [ ] Allow `brew:` to be a **list** in the orchestrator schema, with each item having
      its own `valve:` and optional `flow_meter:` reference (one brew controller per group)
- [ ] Orchestrator manages N independent brew state machines simultaneously
- [ ] This is a significant orchestrator change — **low priority**; defer to Phase 12+
- [ ] Create `examples/two_group_commercial.yaml` — reference config

### 13.2  Spring Lever Machines

Spring lever machines (La Pavoni, Elektra, Cremina, Flair Pro) have **no pump**.
Pressure is generated by a user-operated lever compressing a spring (~8–10 bar at peak).

```
User pulls lever → spring loaded → releases → spring pushes piston → 9 bar peak → declines
```

The pressure profile is **mechanically fixed** by the spring constant.

**ESPHome integration possibilities (monitoring only):**
- Temperature sensor (NTC on boiler) + PID heater control — full support today
- Flow meter on brew path — volume measurement possible
- Shot timer via ESPHome button press
- Group head thermometer via NTC at group inlet
- **No pump control possible** (no pump to control)

**Unsupported today (and possibly forever):**
- There is no electrical actuator on the brew circuit to control
- Pressure sensing is possible (adding a 0–16 bar transducer at group head outlet)
  but pressure profiling is not — the spring determines the curve

### 13.3  E61 Flow Control Device (Manual Needle Valve)

The **E61 flow control device** is a needle valve installed between the pump and the
E61 group head.  Manually restricting the valve limits flow and therefore pressure at
the puck — a mechanical proxy for flow profiling.

**ESPHome integration:**
- If the needle valve is motorised (BLDC servo or stepper motor), it can be controlled
  by an additional ESPHome `stepper:` or `servo:` entity
- The orchestrator would need a `flow_restrictor:` key — currently unsupported
- **Today:** Manual valve + ESPHome temperature + flow meter + shot timer is the
  practical combination; no valve motor control in scope

### 13.4  Induction Heating

Some machines (Breville Barista Touch, some custom builds) use **induction coils**
rather than resistance heating elements.  The induction coil is driven by a
high-frequency resonant inverter; control is via PWM duty cycle of the inverter enable
signal.

**ESPHome compatibility:**
- The inverter's enable pin can be connected to `output: slow_pwm` just like an SSR,
  provided the inverter accepts a slow digital enable rather than requiring fast RF
  modulation
- If the inverter has a PWM speed input (common on off-the-shelf induction modules),
  this maps to `output: ledc` or a standard PWM output
- **Risk:** Induction coils heat much faster than resistance elements — PID parameters
  need aggressive derivative terms and very short periods
- **Safety:** Same hard over-temperature limit applies; see the safety rules in the
  custom instruction preamble

### 13.5  Machines with Proprietary Digital Bus Protocols

Several consumer espresso machines use **closed proprietary digital protocols**
between the controller and heater/pump (Jura GIGA, DeLonghi Dinamica, Breville
Oracle, Melitta high-end):

- **ESPHome integration: not possible** without reverse-engineering the protocol
- Relay bypass of the pump/valve at the 230V level may be possible but bypasses
  all original safety interlocks
- These machines are **explicitly out of scope** for this firmware

### 13.6  Machines without an Accessible Pump (Pod/Capsule Machines)

Nespresso, Dolce Gusto, and most single-serve pod machines have the pump mechanically
integrated with the capsule piercing mechanism.  The pump is not separately accessible
and shot timing is controlled by the OEM controller.

**ESPHome integration: not possible** at the pump or valve level.  Temperature
monitoring with an NTC added externally is the only feasible integration.

### 13.7  Rotary Pump with Bypass Valve (Pressure Regulation via OPV)

Commercial and high-end prosumer machines use a **rotary pump at fixed speed** with an
**Over-Pressure Valve (OPV)** — a spring-loaded bypass that recirculates water when
pressure exceeds the set point (typically 9 bar).

This is the most common commercial architecture.  The OPV mechanically caps pressure;
flow and pressure at the puck are controlled by the portafilter resistance (grind
size and dose), not by pump speed.

**ESPHome integration today:** Full support — the pump is a simple on/off relay; the
OPV is passive hardware; temperature and flow measurement are standard.

**Profiling with an OPV machine:** Only flow profiling via a motorised needle valve
(§13.3) or by adding a variable-speed motor controller and bypassing the OPV at lower
speeds.  Pressure profiling requires removing or adjusting the OPV, which is
non-trivial.

### 13.8  Dual-Boiler with Group-Head Saturated Connection (La Marzocca Style)

La Marzocca Linea, GS3, and similar machines use a **saturated group head** where
the brew boiler water flows through the group head body constantly (via a small
thermosyphon or forced circulation), maintaining the group head at exactly brew
temperature.

```
Brew boiler (93°C) ────► Solenoid ────► Group head ────► Puck
         ↑                                   │
         └─────────── thermosyphon return ───┘
```

**Key difference from E61:** In E61, the thermosyphon is a heat transfer mechanism
only.  In a saturated group, actual brew water fills the group head cavity and returns
to the boiler — so the group head is always primed with brew-temperature water.

**ESPHome integration:**
- Full support today: the brew boiler PID + brew solenoid + flow meter is the same
  as any other dual-boiler configuration
- No additional schema changes needed
- The thermosyphon circulation requires no electrical control (passive convection)

### 13.9  Open Edge Cases and Unsupported Scenarios Summary

| Scenario | Status | Notes |
|---|---|---|
| Water inlet pre-heater | ✅ Works today; orchestrator support planned (§10) | NTC + slow PWM + PID — native ESPHome |
| HX machine / E61 | ✅ Works today with manual flush; `pre_flush_volume:` planned (§11) | — |
| Variable-speed vibration pump | ✅ Works with `type: dimmer` | Limited speed range |
| Variable-speed rotary pump | 🚧 Planned — `pump_profile:` key (§12) | PWM or VFD interface |
| Spring lever machine | ✅ Temperature + flow monitoring; no pump control | Monitoring only |
| E61 motorised needle valve | ⬜ Not in scope | Would need `stepper:` entity |
| Induction heating | 🚧 Probably works with slow_pwm; needs testing | Aggressive PID tuning |
| Proprietary digital bus (Jura, DeLonghi) | ❌ Not possible | Protocol unknown |
| Pod/capsule machines | ❌ Not possible | Pump not accessible |
| OPV rotary pump (commercial) | ✅ Full support | OPV is passive hardware |
| Saturated group head (La Marzocca) | ✅ Full support | Same as dual boiler |
| Multi-group (2–3 groups) | ⬜ Future — list-style `brew:` schema | Low priority |
| Pneumatic pre-infusion | ⬜ Not in scope | No electrical actuator |
| Plumbed-in (no reservoir) | ✅ Flow meter still works; no "tank empty" detection | Remove empty-tank logic |

---

## 14  New Components Required

### 14.1  Summary table

| Component | Machine types | Priority | Effort |
|---|---|---|---|
| `MULTI_CONF` on `espresso_machine_heater` | Dual thermoblock, dual boiler | **High** | Trivial (1 line) |
| Optional `brew_timeout:` in brew schema | Minimal / GaggiaMate | **High** | Low |
| Optional `flow_max` / `flow_offset` defaults | Minimal / GaggiaMate | **High** | Low |
| Allow shared `valve:` + `purge_valve:` id | GaggiaMate / single-boiler | **High** | Low |
| Optional `brew: inlet_heater:` + `inlet_target_temperature:` | Water inlet pre-heater | Medium | Low |
| Optional `brew: pre_flush_volume:` / `pre_flush_timeout:` | HX machine pre-flush | Medium | Low |
| Optional `brew: pump_profile:` | Variable-speed rotary pump profiling | Medium | Medium |
| Optional `cool_pump:` + `cool_pump_profile:` in brew schema | Dual pump / temp profiling | Medium | Medium |
| Optional `secondary_sensor:` on `espresso_machine_heater` | Dual NTC | Medium | Medium |
| Optional `cool_down_to:` in steam schema | Dual boiler | Medium | Low |
| `espresso_machine_pressure:` platform | Dual boiler / profiling | Low | High |

### 14.2  `espresso_machine_pressure` (future)

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

## 15  Orchestrator Extensions Required

### 15.1  Schema changes

| Change | Reason | Breaking |
|---|---|---|
| `brew: flow_max:` — make optional (default 999 ml) | No flow meter support | No |
| `brew: flow_offset:` — make optional (default 0 ml) | No flow meter support | No |
| `brew: valve:` — make optional | Minimalist builds | No |
| `brew: brew_timeout:` — new optional key | Time-based shot termination | No |
| `steam: cool_down_to:` — make optional | Dual boiler (always hot) | No |
| `brew: cool_pump:` — new optional key | Active temp descent profiling | No |
| `brew: cool_pump_profile:` — new optional list | Time-based cool pump blend ramp | No |
| `brew: inlet_heater:` — new optional key | Water inlet pre-heater | No |
| `brew: inlet_target_temperature:` — new optional key | Inlet pre-heater setpoint | No |
| `brew: pre_flush_volume:` — new optional key | HX machine cooling flush (volume) | No |
| `brew: pre_flush_timeout:` — new optional key | HX machine cooling flush (time) | No |
| `brew: pump_profile:` — new optional list | Variable-speed pump profiling | No |
| Allow `brew: valve:` and `brew: purge_valve:` to share same id | Single solenoid machines | No |

### 15.2  C++ changes

| Change | File | Effort |
|---|---|---|
| `brew_timeout_ms_` field + timer check in `BREWING` tick | `espresso_machine.cpp` | Low |
| Null-check before using `brew_valve_` in BrewController | `espresso_machine.cpp` | Low |
| `cool_pump_` pointer + profile interpolation in `BREWING` tick | `espresso_machine.cpp` | Medium |
| `cool_pump_` full-on in `COOLING` state (post-steam secondary benefit) | `espresso_machine.cpp` | Low |
| `IHeater::get_secondary_temperature()` default impl | `interfaces.h` | Low |
| Dual-id validation: warn if `brew_valve_` and `brew_purge_valve_` point to same object | `espresso_machine.cpp` | Low |

---

## 16  Component Reuse Summary

### 16.1  Components reusable as-is (zero changes)

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

### 16.2  Components needing minor changes

| Component | Change needed | Effort |
|---|---|---|
| `espresso_machine_heater` | Add `MULTI_CONF = True`; add `secondary_sensor:` key | Trivial + Low |
| `espresso_machine` orchestrator | See §15 | Low–Medium total |

### 16.3  New components (not yet written)

| Component | Purpose | Needed for |
|---|---|---|
| `espresso_machine_pressure` | Pressure transducer platform | Pressure profiling (future) |

---

## 17  Phased Implementation Plan

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

### Phase E — Dual NTC & Dual Pump (Temperature Descent) Examples

Tasks:
- [ ] `espresso_machine_heater:` — add `secondary_sensor:` optional key
- [ ] Add `IHeater::get_secondary_temperature()` default in `interfaces.h`
- [ ] Add optional `cool_pump:` and `cool_pump_profile:` to `brew:` schema and C++
- [ ] Implement profile interpolation in orchestrator `BREWING` tick
- [ ] Implement cool pump full-on in `COOLING` state for post-steam flush
- [ ] Create `examples/dual_ntc.yaml`
- [ ] Create `examples/dual_pump_temp_descent.yaml` — Decent-style temperature descent
- [ ] Unit tests: profile interpolation at t=0/mid/end; cool pump state in BREWING/COOLING
- [ ] Validate in CI

### Phase F — Pressure Transducer Platform (Future)

> **Depends on Phase 12 (brew profiles) for pressure-based exit conditions.**

Tasks:
- [ ] Create `components/espresso_machine_pressure/__init__.py`
- [ ] C++ `IPressure` interface in `interfaces.h`
- [ ] Wire `IPressure` into brew state machine for pressure-based phase transitions
- [ ] Create `examples/with_pressure_transducer.yaml`
- [ ] Document transducer wiring and calibration in `docs/wiring.md`

### Phase G — Inlet Pre-Heater and HX Machine Support

Tasks:
- [ ] Add optional `brew: inlet_heater:` and `inlet_target_temperature:` to orchestrator schema
- [ ] Add `PRE_FLUSHING` → `FLUSH_PAUSE` states to brew state machine
- [ ] Add optional `brew: pre_flush_volume:` and `pre_flush_timeout:` schema keys
- [ ] C++ null-checks for all new optional pointers
- [ ] Create `examples/with_inlet_preheater.yaml` — thermoblock + inlet pre-heater config
- [ ] Create `examples/hx_e61.yaml` — HX machine reference config
- [ ] Document inlet pre-heater wiring in `docs/wiring.md`
- [ ] Document HX temperature surfing and pre-flush in `docs/pid_tuning.md`
- [ ] Unit tests: inlet not-ready blocks BREWING; pre-flush completes before BREWING starts
- [ ] Validate in CI

### Phase H — Variable-Speed Pump Profile and Closed-Loop Pressure (Future)

> **Open-loop phase (no transducer) can be implemented independently of Phase F.**

Tasks:
- [ ] Add optional `brew: pump_profile:` key — speed fraction ramp (open-loop)
- [ ] Implement pump profile interpolation in orchestrator `BREWING` tick
- [ ] Share `BrewProfileInterpolator` utility with `cool_pump_profile:` interpolation
- [ ] When `espresso_machine_pressure` is available: extend `pump_profile:` to accept
      `target_bar:` entries and implement pressure PID loop in BREWING tick
- [ ] Create `examples/rotary_pump_profiling.yaml` — pre-infusion ramp profile
- [ ] Unit tests: pump speed set correctly at each profile waypoint
- [ ] Validate in CI

---

## 18  Open Questions

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

6. **Cool pump flow calibration:** The temperature model in §9.4 requires knowing Q_hot and
   Q_cool in absolute mL/s.  However, both pumps' actual flow rate at operating pressure
   depends on pump-to-pump variance, check valve cracking pressure, and puck resistance.
   Should we add a guided calibration routine (e.g., run each pump alone for N seconds
   through the flow meter, record volume) to derive `cool_pump_max_flow_ml_s:` at build
   time?  Or leave calibration to the user via the `flow_fraction` values?

7. **Inlet pre-heater placement:** Should the inlet NTC be placed before the pre-heater
   element (to measure incoming water temperature) or after it (to measure the output
   temperature that the main boiler receives)?  The PID input should be the output
   temperature (after the heater).  A second NTC at the inlet would enable a feedforward
   term to compensate for seasonal tap water temperature variation (summer vs. winter
   ambient).

8. **HX cooling flush volume vs. temperature:** The "correct" flush volume for an HX
   machine depends on how long the machine has been idle and the machine's thermal mass.
   Should the flush be time-based, volume-based, or temperature-driven (flush until a
   group-head NTC reads below a threshold)?  Temperature-driven is most accurate but
   requires a group-head NTC sensor (see §8 Dual NTC).

9. **Motorised needle valve (E61 flow control):** If a user adds a stepper-driven needle
   valve to an E61 group head for automated flow profiling, should this be a new
   `espresso_machine_flow_restrictor` platform, or reuse the existing `stepper:` native
   ESPHome entity with an ESPHome automation?  The former is cleaner but adds code; the
   latter is flexible but harder to integrate with the brew state machine.

10. **Multi-group orchestrator architecture:** For a two-group commercial machine, should
    the orchestrator support a list of `brew:` blocks (each with its own valve, flow meter,
    and pump), or should each group be an independent `espresso_machine` instance referencing
    the same shared boiler heater?  The latter is architecturally simpler but requires the
    heater entity to support concurrent references from multiple orchestrators.

---

## Related Documents

| Document | Relevance |
|---|---|
| [`docs/esphome_redesign.md`](esphome_redesign.md) | Decoupling proposals (Proposals A, C, E) that enable multi-machine support |
| [`docs/profiles.md`](profiles.md) | Brew profiles — multi-phase curves apply to all machine types |
| [`docs/scales.md`](scales.md) | Scale platform — weight-based exit applies to all machine types |
| [`docs/wiring.md`](wiring.md) | Current wiring guide (Philips Barista Brew only — will be extended) |
| [`docs/pid_tuning.md`](pid_tuning.md) | PID guide — will be extended with boiler-specific tuning |
| [`PLAN.md`](../PLAN.md) | Phased roadmap — Phases A–H above will be added here |
| [`FEATURES.md`](../FEATURES.md) | Feature status — update when Phase A tasks complete |
| [`examples/philips_barista_brew.yaml`](../examples/philips_barista_brew.yaml) | Current reference (single thermoblock) |
