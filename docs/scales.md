# Scale Integration — Plan & Architecture

> **Status: Planned — no code written yet.**
> This document captures the architecture design, YAML API, supported hardware, and
> implementation roadmap for the `espresso_machine_scale` platform.
> Implementation is tracked in PLAN.md (Phase 13).

---

## Overview

A scale platform makes **weight-based shot exit conditions** possible for both grinding
and brewing.  Without a scale, the only exit condition is volumetric (flow meter).
Weight is more accurate: it accounts for varying puck density, basket size, and grind
distribution, and it directly measures what ends up in the cup.

The scale platform is designed as a **first-class ESPHome entity** — exactly like
`espresso_machine_flow_meter`.  It can be used standalone from Home Assistant or wired
into the orchestrator for automatic weight-based termination.

Two hardware categories are supported:

| Category | How it connects | Examples |
|---|---|---|
| **Bluetooth** | ESP32 BLE stack | Acaia Lunar, Acaia Pearl, Bookoo, Felicita Arc, Difluid Microbalance |
| **Wired load cell** | HX711 / NAU7802 I²C ADC | Any HX711 or NAU7802 based load cell |

Both expose **the same ESPHome entity interface** — the orchestrator does not know (or
care) whether the weight reading comes from a BLE packet or an I²C ADC.

---

## Architecture Design

### Component: `espresso_machine_scale`

```
espresso_machine_scale
│
├── type: bluetooth        ← BLE connection; reads weight from a supported scale protocol
│   ├── mac_address:       ← BLE MAC of the scale
│   └── model:             ← protocol driver (acaia_lunar | acaia_pearl | bookoo | felicita_arc | difluid | auto)
│
└── type: load_cell        ← Wired load cell via HX711 or NAU7802
    ├── variant: hx711     ← HX711 chip
    │   ├── dout_pin:
    │   └── sck_pin:
    └── variant: nau7802   ← NAU7802 chip (I²C)
        ├── sda_pin:
        └── scl_pin:
```

### Entity interface (shared by both types)

The component registers itself as an ESPHome **sensor** platform, exposing:

| Child entity | Unit | Description |
|---|---|---|
| `weight_sensor` | g | Current weight on the scale (tared) |
| `flow_sensor` | g/s | Weight flow rate (first derivative; rolling 3 s window) |

It also exposes actions:

| Action | Description |
|---|---|
| `espresso_machine_scale.tare` | Zero the scale |
| `espresso_machine_scale.start_timer` | Reset and start the built-in shot timer |
| `espresso_machine_scale.stop_timer` | Stop the built-in shot timer |

### Integration point with the orchestrator

The `espresso_machine` orchestrator references the scale by `id:` in its `brew:` block,
exactly as it references `flow_meter:`:

```yaml
espresso_machine:
  id: my_espresso
  brew:
    scale: my_scale                # optional — weight-based exit when present
    target_weight: 36g             # stop when this weight is reached in the cup
    flow_meter: brew_flow          # still used for pre-infusion volume
```

When both `scale:` and `flow_meter:` are wired, the **scale takes priority** for shot
termination; the flow meter is still used for pre-infusion volume tracking.

The grinder also gains an optional weight-based exit:

```yaml
espresso_machine_grinder:
  id: main_grinder
  scale: main_scale        # optional — grind until target_dose is reached
  target_dose: 18g         # stop grinding when this weight of coffee is in the portafilter
```

---

## Supported Bluetooth Scales

### Protocol support matrix

| Scale | BLE name | Protocol | Tare command | Real-time flow | Timer commands | Notes |
|---|---|---|---|---|---|---|
| **Acaia Lunar** | `LUNAR-*` | Acaia v1 (proprietary, reverse-engineered) | ✅ | ✅ | ✅ | Most popular specialty coffee scale |
| **Acaia Pearl** | `PEARL-*` | Acaia v1 | ✅ | ✅ | ✅ | Desk version of Lunar |
| **Acaia Pearl S / Pearl 2021** | `PEARLS-*` | Acaia v2 | ✅ | ✅ | ✅ | Updated protocol — separate driver |
| **Bookoo** | `Bookoo*` / `Felicita*` | Felicita Arc protocol | ✅ | ✅ | ✅ | Also covers Felicita Arc rebrands |
| **Felicita Arc** | `FELICITA*` | Felicita Arc protocol | ✅ | ✅ | ✅ | Same protocol as Bookoo |
| **Difluid Microbalance** | `DFRobot*` / `Difluid*` | Difluid proprietary | ✅ | ✅ | ⬜ | Popular budget scale with BLE |
| **Hiroia Jimmy** | `JIMMY*` | Hiroia protocol | ⬜ | ⬜ | ⬜ | Planned; protocol documented in open source |
| **Timemore Black Mirror** | `BM*` | Timemore BLE | ⬜ | ⬜ | ⬜ | Planned |
| **Generic ESPHome BLE sensor** | any | `ble_sensor` passthrough | ✅ | via HA | via HA | Fallback: use native `sensor.ble_sensor` |

> **Implementation note:** All BLE scale protocols listed above have been reverse-engineered
> and are publicly documented.  The Acaia Lunar protocol is documented in multiple open-source
> projects (acaia-py, decent_tablet, mrheat).  The Felicita/Bookoo protocol is documented in
> the acaia-py and coffee-scale libraries.

### Why ESP32 connects to the scale (not Home Assistant)

Several alternative connection strategies exist:

| Strategy | Pros | Cons |
|---|---|---|
| **ESP32 → BLE → Scale** (chosen) | No HA dependency; low latency (~50 ms); works offline; single network hop; tare/timer commands work | Needs BLE-capable ESP32 (all modern ones qualify) |
| HA + BTProxy → Scale → HA → ESPHome | Simpler firmware | 500 ms+ round trip; tare commands complex; HA dependency |
| Dedicated BLE-to-MQTT bridge | Decoupled | Extra hardware; MQTT dependency |

The chosen approach (ESP32 owns the BLE connection) gives **50 ms or better** weight
update latency — sufficient for gram-accurate shot termination.  The tare and timer
commands are sent directly from firmware without round-tripping through HA.

### ESP32 BLE constraints

- **BLE + Wi-Fi co-existence:** ESP32 uses a shared radio for BLE and Wi-Fi.  The
  `esp32_ble_tracker` component in ESPHome already handles co-existence.  The scale
  component will use the same BLE stack.
- **Connection state:** The component maintains a persistent BLE connection to the scale.
  On disconnect, it retries every 5 s.  Weight readings are marked stale after 2 s
  without a packet.
- **Power:** BLE peripheral mode adds ~10 mA to ESP32 current draw — negligible given the
  machine's power supply.
- **Concurrent connections:** ESP32 can maintain up to 3 simultaneous BLE connections
  (one scale is sufficient; a second could be used for a portafilter dose scale).

---

## Wired Load Cell Option

For users who prefer a fully wired, no-RF solution:

### HX711

```yaml
espresso_machine_scale:
  id: cup_scale
  name: "Cup Scale"
  type: load_cell
  variant: hx711
  dout_pin: GPIO32
  sck_pin: GPIO33
  gain: 128           # 64 or 128; 128 = higher resolution, ±20 mV input range
  sample_rate: 10     # Hz (HX711 supports 10 or 80 Hz)
  calibration_factor: 420.0   # raw units per gram; determined at calibration
  weight_sensor:
    name: "Cup Weight"
    unit_of_measurement: g
    accuracy_decimals: 1
  flow_sensor:
    name: "Cup Flow Rate"
    unit_of_measurement: "g/s"
    accuracy_decimals: 2
```

The HX711 is widely available, inexpensive (~$1–$2), and well-supported.  It is
already used in countless kitchen scale projects and is natively supported in ESPHome
via the `hx711` sensor platform.  The `espresso_machine_scale` load-cell variant
wraps that platform and adds the flow rate derivative and tare action.

### NAU7802

```yaml
espresso_machine_scale:
  id: cup_scale
  name: "Cup Scale"
  type: load_cell
  variant: nau7802
  address: 0x2A      # fixed I²C address
  gain: 128
  sample_rate: 80    # Hz (NAU7802 supports 10, 20, 40, 80, 320 Hz)
  calibration_factor: 210.0
  weight_sensor:
    name: "Cup Weight"
  flow_sensor:
    name: "Cup Flow Rate"
```

The NAU7802 is preferred for higher sample rates (up to 320 Hz) and better noise
immunity.  It is used in the OpenScale and Acaia-compatible DIY scale projects.

### Wired scale bill of materials

| Component | Purpose | Approx. cost |
|---|---|---|
| Load cell (1 kg) | Force sensor | $3–$10 |
| HX711 breakout | ADC amplifier | $1–$2 |
| OR NAU7802 breakout | Higher-res ADC | $5–$10 |
| 3D-printed platform | Holds espresso cup over load cell | Free (print yourself) |
| M2.5 screws + standoffs | Assembly | $1 |

---

## Mock Scale (`espresso_machine_mock_scale`)

> **Status: Planned — no code written yet.**
> The mock scale is a software-only simulation component that lets you develop and test
> weight-based brew exit and grinder dosing **without any physical scale hardware**.
> It follows the same pattern as `espresso_machine_mock_heater` and
> `espresso_machine_mock_pump`.

### Purpose

The mock scale implements the same `IScale` interface as the real scale drivers.
The orchestrator and grinder cannot tell the difference — they call `get_weight_g()`,
`is_connected()`, and `tare()` identically regardless of whether the weight comes
from a BLE packet, an ADC, or the mock model.

This lets you:
- Test weight-based brew exit in simulation, without a physical scale.
- Test grinder dose-by-weight in simulation, without a portafilter scale.
- Verify tare-on-brew-start and tare-on-grind-start behaviour.
- Run the full CI test suite for scale-related features in a host environment.

### Two simulation modes

| Mode | Use case | Weight source |
|---|---|---|
| **Cup scale** (brew) | Simulates weight accumulating in the espresso cup | Derives weight from the mock pump's `nozzle_total` output |
| **Portafilter scale** (grinder) | Simulates coffee falling into the portafilter | Accumulates at a configurable `dose_rate_g_per_s` while grinder is running |

Both modes are configured on the same `espresso_machine_mock_scale:` component.
A single instance can be used as a cup scale (wired to `brew: scale:`), a portafilter
scale (wired to grinder `scale:`), or both at once with two separate instances.

### Cup scale model (brew)

The cup scale derives its weight reading from the **nozzle output of
`espresso_machine_mock_pump`**.  The pump's nozzle flow model already accounts for
puck water absorption, so `nozzle_total_volume_ml` represents the actual liquid
that exits the group head into the cup.

Since espresso has a density of approximately 1 g/mL, the relationship is:

```
weight_g(t) = nozzle_total_volume_ml(t) × liquid_density_g_per_ml
```

The default `liquid_density_g_per_ml` is `1.05` (espresso is slightly denser than water
due to dissolved solids).  This is configurable as a HA number entity for tuning.

The weight is reset to zero (tared) automatically when `brew_start()` is called, before
the pump starts.

**Example flow during a simulated shot:**

```
t=0 s   brew_start() called → scale tared (weight=0)
t=1 s   pump starts, nozzle output begins building
t=5 s   puck absorbs most water; nozzle_total ≈ 0.5 mL → weight ≈ 0.5 g
t=15 s  puck largely saturated; nozzle_total ≈ 12 mL → weight ≈ 12.6 g
t=25 s  nozzle_total ≈ 28 mL → weight ≈ 29.4 g
t=28 s  weight crosses target_weight (36 g) → brew_stop() triggered
```

### Portafilter scale model (grinder)

The portafilter scale simulates coffee falling into the portafilter basket at a
fixed rate while the grinder relay is active.

```
weight_g(t) = dose_rate_g_per_s × grinder_active_time_s
```

The weight is reset to zero (tared) automatically when `grinder.grind()` is called.
The grinder stops when `weight_g >= target_dose` or `dose_timeout` elapses.

**Example flow during a simulated grind:**

```
dose_rate_g_per_s = 2.0
target_dose = 18 g

t=0 s   grind_start() → portafilter scale tared (weight=0)
t=1 s   weight ≈ 2 g
t=5 s   weight ≈ 10 g
t=9 s   weight ≈ 18 g → dose reached → grinder stops
```

With `dose_rate_g_per_s = 2.0` and `target_dose = 18 g`, grind time ≈ 9 s.
This matches typical single-boiler grinder performance (1–3 g/s is realistic).

### Auto-tare behaviour

| Event | Automatic tare |
|---|---|
| `brew_start()` called | ✅ Cup scale tared before pump starts |
| `grinder.grind()` called | ✅ Portafilter scale tared before motor starts |
| Manual `espresso_machine_scale.tare` action | ✅ Always available |

The tare zeroes the internal `tare_offset_g_` so that subsequent weight readings
start from zero, matching real-scale behaviour.  The tare value is **not** persisted
across reboots in the mock (no `globals:` needed — the mock always starts at 0 g).

### YAML — mock scale (cup weight from pump nozzle output)

```yaml
external_components:
  - source: github://shaggitza/test_espresso_esphome@main
    components:
      - espresso_machine_mock_scale   # add to existing mock component list

espresso_machine_mock_scale:
  id: cup_scale
  name: "Mock Cup Scale"
  mock_pump: main_pump              # reference to espresso_machine_mock_pump
  liquid_density_g_per_ml: 1.05     # espresso ≈ 1.05 g/mL; water = 1.0 g/mL
  weight_sensor:
    name: "Cup Weight (Simulated)"
    unit_of_measurement: g
    accuracy_decimals: 1
  flow_sensor:
    name: "Cup Flow Rate (Simulated)"
    unit_of_measurement: "g/s"
    accuracy_decimals: 2
  # Optional runtime-tunable number entity:
  liquid_density_number:
    name: "Mock Liquid Density"
    entity_category: config
```

### YAML — mock portafilter scale (dose by weight from grinder)

```yaml
espresso_machine_mock_scale:
  id: portafilter_scale
  name: "Mock Portafilter Scale"
  dose_rate_g_per_s: 2.0            # grams of coffee per second while grinder runs
  weight_sensor:
    name: "Portafilter Weight (Simulated)"
  # Optional runtime-tunable number entity:
  dose_rate_number:
    name: "Mock Dose Rate"
    entity_category: config
```

### Wiring the mock scale into the orchestrator and grinder

```yaml
espresso_machine:
  id: my_espresso
  brew:
    pump: main_pump
    scale: cup_scale                # mock scale derives weight from pump nozzle output
    target_weight: 36g
    flow_max: 45ml                  # safety fallback if scale goes stale

espresso_machine_grinder:
  id: main_grinder
  scale: portafilter_scale          # mock portafilter scale
  target_dose: 18g
  dose_timeout: 30s
```

### Physics parameters (HA number entities)

All mock scale parameters are exposed as Home Assistant `number` entities so they can
be adjusted at runtime without reflashing:

| HA entity | Default | Range | Description |
|---|---|---|---|
| `"Mock Liquid Density"` | 1.05 g/mL | 0.9–1.2 | Espresso density; scales cup weight from nozzle volume |
| `"Mock Dose Rate"` | 2.0 g/s | 0.1–10.0 | Coffee output rate from grinder into portafilter |

### Scenario coverage enabled by mock scale

| Scenario | Coverage | Notes |
|---|---|---|
| Brew exits at `target_weight` (cup scale) | ✅ Full | Nozzle volume × density drives weight to target |
| Stale scale fallback to `flow_max` (cup scale) | ✅ Full | Set `mock_pump: none` or disconnect pump reference |
| Tare on brew start | ✅ Full | `brew_start()` calls `tare()` before pump activates |
| Grinder stops at `target_dose` (portafilter scale) | ✅ Full | `dose_rate_g_per_s` × time reaches `target_dose` |
| Grinder falls back to `default_grind_time` on stale | ✅ Full | Set `dose_rate_g_per_s: 0` to simulate no-signal |
| Tare on grind start | ✅ Full | `grind_start()` calls `tare()` before motor activates |
| Dual-scale setup (cup + portafilter) | ✅ Full | Two separate `espresso_machine_mock_scale:` instances |

### Implementation plan (Phase 13b — Mock Scale)

- [ ] `components/espresso_machine_mock_scale/__init__.py` — schema with
  `cv.Optional(..., default=...)` for all parameters:
  `mock_pump` (optional reference), `dose_rate_g_per_s` (default 2.0),
  `liquid_density_g_per_ml` (default 1.05),
  `weight_sensor`, `flow_sensor`, `liquid_density_number`, `dose_rate_number`
- [ ] `components/espresso_machine_mock_scale/mock_scale.h` — `MockScale` class
  implementing `IScale`; derives weight from `MockPump::get_nozzle_flow_total()` for
  cup mode and from elapsed grinder time for portafilter mode
- [ ] `components/espresso_machine_mock_scale/mock_scale.cpp` — `loop()` integrates
  nozzle volume, applies density, publishes `weight_sensor` and `flow_sensor`; `tare()`
  stores tare offset; `is_connected()` always returns `true`
- [ ] Add `espresso_machine_mock_scale` to `external_components:` list in
  `examples/philips_barista_brew_mock.yaml`
- [ ] Add mock scale scenario rows to `docs/mock_scenarios.md`

---

## YAML API — Full Reference

### Bluetooth scale (Acaia Lunar example)

```yaml
external_components:
  - source: github://shaggitza/test_espresso_esphome@main
    components:
      - espresso_machine_scale   # add to existing component list

espresso_machine_scale:
  id: cup_scale
  name: "Cup Scale"
  type: bluetooth
  model: acaia_lunar             # acaia_lunar | acaia_pearl | acaia_pearl_s | bookoo |
                                 # felicita_arc | difluid | auto
  mac_address: "AA:BB:CC:DD:EE:FF"
  reconnect_interval: 5s         # retry BLE connection on disconnect
  stale_timeout: 2s              # mark reading invalid after this time without packet
  weight_sensor:
    name: "Cup Weight"
    unit_of_measurement: g
    accuracy_decimals: 1
  flow_sensor:
    name: "Cup Flow Rate"
    unit_of_measurement: "g/s"
    accuracy_decimals: 2
```

### Wired load cell (HX711 example)

```yaml
espresso_machine_scale:
  id: cup_scale
  name: "Cup Scale"
  type: load_cell
  variant: hx711
  dout_pin: GPIO32
  sck_pin: GPIO33
  gain: 128
  sample_rate: 10
  calibration_factor: 420.0
  weight_sensor:
    name: "Cup Weight"
  flow_sensor:
    name: "Cup Flow Rate"
```

### Wiring scale into the orchestrator (brew exit by weight)

```yaml
espresso_machine:
  id: my_espresso
  brew:
    heater: main_heater
    pump: main_pump
    valve: brew_valve
    purge_valve: purge_valve
    flow_meter: brew_flow        # still used for pre-infusion
    scale: cup_scale             # weight-based exit overrides flow_meter exit
    target_weight: 36g           # stop when 36 g is in the cup
    target_temperature: 90°C
    flow_max: 45ml               # safety fallback if scale disconnects
```

### Wiring scale into the grinder (dose by weight)

```yaml
espresso_machine_grinder:
  id: main_grinder
  name: "Grinder"
  type: relay
  pin: GPIO23
  default_grind_time: 7s
  scale: portafilter_scale       # grind until target_dose is reached
  target_dose: 18g               # stop when 18 g of coffee is in portafilter
  dose_timeout: 30s              # safety fallback if dose not reached
```

---

## Orchestrator Behaviour Changes

### Brew: weight-based exit

When `scale:` is configured in `brew:`:

1. At `brew_start()`, the scale is tared and its timer is started.
2. During `BREWING`, the weight is polled every loop tick.
3. When `weight >= target_weight`, `brew_stop()` is called.
4. If the scale reading goes stale (BLE disconnect or bad sensor), the brew falls back
   to the `flow_max` volumetric exit condition.
5. Shot statistics gain two new fields: `last_shot_weight_g` and `last_shot_brew_ratio`.

### Grinder: dose by weight

When `scale:` is configured on the grinder:

1. At grind start, the scale (portafilter scale) is tared.
2. Grinding continues until `weight >= target_dose` or `dose_timeout` elapses.
3. `default_grind_time` is used as a fallback if no scale or scale is stale.

### Priority rules

| Scale present | Flow meter present | Exit condition |
|---|---|---|
| ✅ (reading fresh) | ✅ | Weight (scale takes priority) |
| ✅ (reading stale) | ✅ | Volume (fallback) |
| ✅ (reading fresh) | ❌ | Weight |
| ❌ | ✅ | Volume |
| ❌ | ❌ | Timeout only (not recommended) |

---

## Shot Statistics with Scale

When a scale is wired, the following additional statistics are available as HA sensor
entities:

| Entity | Unit | Description |
|---|---|---|
| `sensor.last_shot_weight_g` | g | Final cup weight when shot stopped |
| `sensor.last_shot_brew_ratio` | — | Yield / dose ratio (e.g. 2.0 for 18 g in → 36 g out) |
| `sensor.last_shot_peak_flow_g_s` | g/s | Peak weight flow rate during extraction |

---

## Home Assistant Integration

All scale entities appear automatically in HA:

- `sensor.<name>_cup_weight` — live cup weight (g)
- `sensor.<name>_cup_flow_rate` — live weight flow (g/s)
- `button.<name>_tare` — one-tap tare button
- `sensor.<name>_last_shot_weight` — final shot weight

A suggested Lovelace card:

```yaml
type: entities
title: Scale
entities:
  - entity: sensor.espresso_cup_weight
    name: Cup Weight
    icon: mdi:scale
  - entity: sensor.espresso_cup_flow_rate
    name: Flow (g/s)
  - entity: button.espresso_tare
    name: Tare
  - entity: sensor.espresso_last_shot_weight
    name: Last Shot Weight
  - entity: sensor.espresso_last_shot_brew_ratio
    name: Brew Ratio
```

---

## Implementation Roadmap (Phase 13)

### Phase 13a — Schema & Interface

- [ ] Create `components/espresso_machine_scale/__init__.py` with schema for both `type:
  bluetooth` and `type: load_cell`
- [ ] Define `IScale` interface in `components/espresso_machine/interfaces.h`:
  - `get_weight_g()` → float
  - `get_flow_g_per_s()` → float
  - `tare()` → void
  - `is_connected()` → bool (BT) / `is_ready()` → bool (load cell)
  - `start_timer()` / `stop_timer()` → void
- [ ] Add `IScale*` pointer to `EspressoMachine` and `GrinderController`
- [ ] Add `target_weight:` to `brew:` sub-schema
- [ ] Add `target_dose:` / `dose_timeout:` to grinder sub-schema
- [ ] Add weight-based exit logic to brew state machine (fallback to volume on stale)
- [ ] Add dose-based exit logic to grinder

### Phase 13b — Wired Load Cell Driver

- [ ] `components/espresso_machine_scale/scale_load_cell.h` / `.cpp`
  - HX711 variant: bit-banged SPI (GPIO dout + sck)
  - NAU7802 variant: I²C via ESPHome `i2c_device`
- [ ] Calibration action: `espresso_machine_scale.calibrate` (known weight → sets factor)
- [ ] Tare offset stored in `globals:` (survives reboot)
- [ ] Unit test: weight reading + flow rate derivative

### Phase 13c — Bluetooth Protocol Drivers

- [ ] `components/espresso_machine_scale/ble/` — shared BLE connection manager
  - Persistent connection with reconnect backoff
  - Stale-reading detection
- [ ] `components/espresso_machine_scale/ble/acaia_v1.cpp` — Acaia Lunar / Pearl driver
  - Characteristic UUID: `0x2A80` (notify)
  - Packet decode: header `0xef 0xdd`, weight bytes, unit, stable flag
  - Tare command: `0xef 0xdd 0x04 …`
  - Timer start/stop commands
- [ ] `components/espresso_machine_scale/ble/acaia_v2.cpp` — Acaia Pearl S / 2021 driver
  - Updated command set; different packet framing
- [ ] `components/espresso_machine_scale/ble/felicita.cpp` — Bookoo / Felicita Arc driver
  - Service UUID `0xFFE0`, characteristic `0xFFE1`
  - Packet decode: 10-byte notification, weight in 0.1 g units
  - Tare command documented in open-source `coffee-scale` library
- [ ] `components/espresso_machine_scale/ble/difluid.cpp` — Difluid Microbalance driver
  - BLE UART service (`0xFFE0` / `0xFFE1`)
  - Weight decode from ASCII-like encoding

### Phase 13d — Integration Tests

- [ ] GoogleTest mock: `MockScale` implementing `IScale`
- [ ] Brew test: weight exit condition triggers `brew_stop()` at `target_weight`
- [ ] Brew test: stale scale falls back to volumetric exit
- [ ] Grinder test: dose exit condition triggers grinder stop
- [ ] Grinder test: stale scale falls back to `default_grind_time`

### Phase 13e — Documentation & Examples

- [ ] Update `examples/philips_barista_brew.yaml` — add commented-out `espresso_machine_scale:`
  block (Bluetooth and load-cell variants)
- [ ] Update `examples/philips_barista_brew_mock.yaml` — add mock scale sensor
- [ ] Update `README.md` — mention scale support in feature table
- [ ] Update `FEATURES.md` — add scale rows
- [ ] Update `docs/wiring.md` — add HX711 and NAU7802 wiring diagrams
- [ ] Update `docs/home_assistant.md` — add scale Lovelace card
- [ ] Update `structure.md` — add `espresso_machine_scale/` to component tree

---

## Open Questions

1. **Dual-scale setup** (portafilter dose scale + cup scale): Should a single YAML allow
   two `espresso_machine_scale:` instances — one referenced by the grinder for dosing,
   one by the brew orchestrator for yield?  The current design supports this via separate
   `id:` references.

2. **BLE scanning vs. direct connection:** Should the component use `esp32_ble_tracker`
   (active scan) or connect directly by MAC?  Direct connection by MAC avoids the scan
   overhead and establishes a persistent GATT link — preferred.

3. **Tare timing:** The scale must be tared *after* the cup is placed but *before* the
   shot starts.  The recommended approach is to use an ESPHome automation that calls
   `espresso_machine_scale.tare` before `espresso_machine.brew_start` — this is already
   expressible today via the `script:` platform.  An optional `auto_tare_on_brew_start:
   true` convenience key will be added to the schema (Phase 13a) to do this automatically
   without a separate script.

4. **Grind-by-weight UX:** When grinding by weight, the user needs feedback that the
   grinder is in dose-mode (not time-mode).  A HA `select` entity (`time` / `weight`)
   on the grinder would make this explicit.

5. **BLE re-pairing:** If the scale's MAC changes (e.g. firmware update), the user must
   update the YAML.  An `auto` model with BLE scanning + name-pattern matching could
   help, at the cost of a 5 s scan window at boot.

6. **Weight unit normalization:** Some scales report in grams, some in ounces.  The
   driver layer should always normalise to grams before publishing.

---

## Reference Open-Source Implementations

The BLE protocols used by supported scales have been reverse-engineered and documented
by the community.  These are the primary references for the driver implementations:

| Project | URL | Protocols covered |
|---|---|---|
| **acaia-py** | https://github.com/lucapinello/pyacaia | Acaia Lunar v1, Pearl v1 |
| **acaia-py (async)** | https://github.com/zweckj/pyacaia_async | Acaia Lunar v1, v2 |
| **coffee-scale** | https://github.com/nickcoutsos/coffee-scale | Bookoo, Felicita, Acaia |
| **mrheat (ESPHome BLE)** | Community forum threads | Acaia Lunar BLE ESPHome example |
| **OpenScale firmware** | https://github.com/sparkfun/OpenScale | HX711 calibration reference |
| **NAU7802 ESPHome** | ESPHome core `nau7802` platform | NAU7802 integration |

---

## Related Documents

| Document | Relevance |
|---|---|
| [`docs/profiles.md`](profiles.md) | Weight-based exit in brew profiles (`exit.weight_g:`) |
| [`docs/wiring.md`](wiring.md) | HX711 / NAU7802 wiring will be added here |
| [`docs/home_assistant.md`](home_assistant.md) | Scale Lovelace cards will be added here |
| [`FEATURES.md`](../FEATURES.md) | Scale feature tracking |
| [`PLAN.md`](../PLAN.md) | Phase 13 roadmap entry |
