# Mock Simulation Scenarios

This document catalogues the physical scenarios that the
`espresso_machine_mock_heater` and `espresso_machine_mock_pump` components
simulate, partially simulate, or deliberately do not simulate — along with a
testing plan for the safety-critical C++ code paths.

---

## Scenario Coverage Matrix

| Scenario | Mock Component | Coverage | Notes |
|---|---|---|---|
| Puck wetting / flow ramp-up | mock_pump | ✅ Full | Exponential ramp, τ scales with puck pressure |
| Pump pressure-flow curve | mock_pump | ✅ Full | Linear vibration-pump curve calibrated at 9 bar |
| Hard / easy puck variation | mock_pump | ✅ Full | Adjustable `puck_pressure_bar` |
| Pump stall (puck ≥ stall pressure) | mock_pump | ✅ Full | Q = 0 when P_puck ≥ P_stall |
| Residual pressure after pump stop | mock_pump | ✅ Full | `internal_volume_ml` governs decay τ |
| Bypass mode (valves open) | mock_pump | ✅ Full | D_eff = 1, max flow, zero pressure when pumping with valve open |
| Flow-based thermoblock cooling | mock_heater | ✅ Full | Heat transfer effectiveness model; ε = 1 − exp(−k/Q) |
| Heat transfer effectiveness tuning | mock_heater | ✅ Full | `heat_transfer_k` parameter, HA-tunable number entity |
| Thermoblock thermal mass | mock_heater | ✅ Full | Al block + water thermal mass |
| Ambient heat loss | mock_heater | ✅ Full | Newton cooling term h × (T − T_amb) |
| Thermal distance water→heater | mock_heater | ✅ Full | `dist_water_to_heater_mm`; heater element node; C_H=10% of mass; τ = C_H/G |
| Thermal distance water→sensor | mock_heater | ✅ Full | `dist_water_to_sensor_mm`; sensor node lags block; PID reads lagged temperature |
| Thermal distance sensor→heater | mock_heater | ✅ Full | `dist_sensor_to_heater_mm`; direct heater→sensor coupling path |
| PID heater control | native ESPHome PID | ✅ Full | Unchanged from real config |
| Temperature overshoot at startup | mock_heater | ✅ Full | Controlled by thermal mass and PID gains |
| Over-temperature safety cutoff | mock_heater + YAML | ✅ Full | `on_value_range` above 165 °C in example YAML |
| Grinder lockout during brew | orchestrator | ✅ Full | State-machine interlock |
| Valve interlock (one open at a time) | valve platform | ✅ Full | Platform-level enforcement |
| Shot auto-termination at `flow_max` | orchestrator | ✅ Full | State machine checks `get_flow_total()` |
| Shot auto-termination at `target_weight` (scale) | mock_scale + orchestrator | ⬜ Planned | `MockScale` derives weight from `nozzle_total`; brew stops when weight ≥ target |
| Stale scale fallback to `flow_max` | mock_scale + orchestrator | ⬜ Planned | Set `dose_rate_g_per_s: 0` or disconnect mock_pump reference |
| Tare on brew start | mock_scale + orchestrator | ⬜ Planned | `brew_start()` calls `IScale::tare()` before pump starts |
| Grinder dose exit at `target_dose` | mock_scale + grinder | ⬜ Planned | `dose_rate_g_per_s × time` reaches `target_dose`; grinder stops |
| Grinder falls back to `default_grind_time` (stale scale) | mock_scale + grinder | ⬜ Planned | Set `dose_rate_g_per_s: 0` to simulate no weight signal |
| Tare on grind start | mock_scale + grinder | ⬜ Planned | `grind_start()` calls `IScale::tare()` before motor starts |
| Pre-infusion hold | orchestrator | ✅ Full | Volume + timer gated phase |
| Steam temperature ramp | mock_heater | ✅ Full | PID drives heater to steam setpoint |
| Steam purge-before-steam (PURGING state) | orchestrator | ✅ Full | Pumps configured volume through purge valve; ensures dry steam |
| Steam safety timeout | orchestrator | ✅ Full | Auto-stop after `timeout` ms if not manually stopped |
| Post-steam cool-down | mock_heater | ✅ Full | Setpoint lowered; ODE cools naturally |
| Pump restart mid-shot (flow reset) | mock_pump | ✅ Full | `run_time_` resets; `system_pressure_bar_` clears |
| Scale / limescale flow restriction | mock_pump | ⚠️ Partial | Approximate by increasing `puck_pressure_bar` |
| Channeling (uneven puck extraction) | mock_pump | ❌ Not covered | See below |
| Thermal runaway | mock_heater | ❌ Not covered | See below |
| Sensor drift / thermocouple failure | mock_heater | ❌ Not covered | See below |
| Water hammer (pump surge) | mock_pump | ❌ Not covered | See below |
| Boiler overfill / flood | — | ❌ Not covered | Hardware guard; out of scope |
| Power loss mid-brew | — | ❌ Not covered | GPIO defaults handle this in hardware |
| Wi-Fi disconnect during brew | orchestrator | ❌ Not covered | ESPHome native keep-alive handles API |
| PID integral wind-up | native PID | ⚠️ Partial | ESPHome PID has built-in anti-windup |

---

## Covered Scenarios in Detail

### Residual Pressure After Pump Stop

**What it models:** When the pump turns off, pressurised water trapped inside
the machine's tubing and group-head piping continues to flow through the puck
until the pressure bleeds off.

**Parameters:**

| Parameter | Default | Effect |
|---|---|---|
| `internal_volume_ml` | 20 mL | Volume of pressurised piping. Larger → slower decay. |
| `nominal_flow_ml_per_s` | 4 mL/s | Sets decay τ: τ = internal_volume / nominal_flow |

**Model:**

```
system_pressure(t) = P_stop × exp(−t / τ_decay)
Q_residual(t)      = Q_ss × (system_pressure(t) / P_puck)
τ_decay            = internal_volume_ml / nominal_flow   [s]
```

With defaults, τ = 20 / 4 = 5 s. Flow is at 37% of steady-state after 5 s and
effectively zero after ~25 s (5 × τ).

**Failure scenarios enabled:**

- **Over-extraction:** orchestrator stops the pump at `flow_max`, but residual
  pressure delivers extra volume through the puck. Test that the orchestrator
  closes the brew valve promptly to prevent this.
- **Valve timing:** if `purge_valve` opens before brew valve closes, residual
  pressure drives water through the wrong path.

**Setting `internal_volume_ml = 0` reverts to instant-decay** (pre-2025
behaviour) for tests that don't need the pressure model.

---

### Heat Transfer Effectiveness Model

**What it models:** When water flows through the thermoblock, it absorbs heat
from the aluminium block. At low flow rates, water has time to reach thermal
equilibrium with the block (high effectiveness). At high flow rates, water
exits before fully heating (lower effectiveness per mL, but more total heat
transfer due to higher volume).

**Parameters:**

| Parameter | Default | Effect |
|---|---|---|
| `heat_transfer_k` | 2.0 mL/s | Characteristic flow rate for heat transfer. Lower → better transfer at low flows. |
| `water_inlet_temp_c` | 20 °C | Temperature of incoming cold water. |

**Model:**

```
ε = 1 − exp(−k / Q)                           # Heat transfer effectiveness
Q_water = Q × Cp × ε × (T_block − T_inlet)    # Heat absorbed by water [W]

where:
  Q     = flow rate [mL/s]
  k     = heat_transfer_k [mL/s]
  Cp    = 4.186 J/(mL·°C)
  ε     = effectiveness (0 to 1)
```

**Behaviour at limits:**
- **Q << k:** ε ≈ 1 (water fully absorbs available heat)
- **Q >> k:** ε ≈ k/Q (less efficient per mL; total heat flow ~ k × Cp × ΔT)
- **Q = k:** ε ≈ 0.63 (63% effectiveness)

**Applies during all flow scenarios:** brewing, steaming, flushing, cooling.
The `loop()` function continuously calculates heat transfer whenever flow
rate > 0, regardless of orchestrator state.

**Runtime tuning:** Adjust `heat_transfer_k` via the HA number entity
`"Mock Heat Transfer K"` to match your machine's actual thermoblock
characteristics without reflashing.

---

### Thermal Distance Model — Finite-Difference Diffusion Chain

**What it models:** In a real thermoblock the heater element, water channel,
and temperature-sensor probe are all embedded at different locations in an
aluminium body. Heat must diffuse through aluminium to travel between them.
This introduces two physically distinct effects:

1. **Lag (dead time):** No response at the far end until the heat wavefront
   arrives — proportional to distance².
2. **Spatial averaging:** The sensor receives a time-weighted average of the
   heat source history, not an instantaneous reading. The impulse response is
   Gaussian-shaped (peaks at t_peak = d²/(6α)), not a delta function.

These effects are fundamentally different from a simple 1st-order RC lag (which
would show an immediate exponential response with no dead time). They make the
PID controller harder to tune in a physically accurate way.

**Parameters:**

| Parameter | Default | Units | Effect |
|---|---|---|---|
| `dist_water_to_heater_mm` | 0 | mm | Distance through Al from heater element to water contact zone |
| `dist_water_to_sensor_mm` | 0 | mm | Distance through Al from water contact to sensor probe |
| `dist_sensor_to_heater_mm` | 0 | mm | Direct Al path from heater element to sensor probe |

Setting all three to 0 (default) reverts to the original 1-node model —
backward-compatible with all existing tests and configurations.

**Thermal nodes:**

When any distance > 0 a 3-node thermal network is activated:

| Node | Label | Thermal mass share | Role |
|---|---|---|---|
| Heater element | H | 10 % of `thermal_mass_j_per_c` | Receives electrical power |
| Block / water contact | W | 90 % of `thermal_mass_j_per_c` | Where flow cooling and ambient loss act |
| Sensor probe | S | 5 % of `thermal_mass_j_per_c` | What the PID reads |

**Finite-difference diffusion chain (N_THERMAL_SEGS = 4):**

Each non-zero distance activates a finite-difference diffusion chain of
4 segments between the two endpoint nodes. This approximates true 1D heat
conduction through an aluminium rod (heat equation: ∂T/∂t = α ∇²T).

Aluminium material constants (physical, fixed):
```
K_Al  = 200  W/(m·K)     — thermal conductivity
ρ_Al  = 2700 kg/m³       — density
Cp_Al = 897  J/(kg·K)    — specific heat
α_Al  = K_Al/(ρ_Al×Cp_Al) ≈ 82.6 mm²/s   — thermal diffusivity
A_ref = 1e-4 m² (1 cm²)  — representative cross-section
```

Segment properties for a path of length d_mm:
```
Δx    = d_m / 4                          — segment length [m]
G_seg = K_Al × A_ref / Δx               — segment conductance [W/K]
C_seg = ρ_Al × Cp_Al × A_ref × Δx      — segment thermal mass [J/K]
τ_seg = C_seg / G_seg = Δx² / α_Al     — segment time constant [s]
```

For d = 10 mm (Δx = 2.5 mm):
```
G_seg = 200 × 1e-4 / 2.5e-3 = 8 W/K
C_seg = 2700 × 897 × 1e-4 × 2.5e-3 = 0.605 J/K
τ_seg = 0.605 / 8 ≈ 76 ms
```

Chain integration (explicit Euler with automatic sub-stepping):
```
Stability: dt_sub ≤ 0.4 × τ_seg  →  n_substeps ≥ dt_outer / (0.4 × τ_seg)
Update:    dT_i = dt_sub × (G_seg/C_seg) × (T_{i-1} + T_{i+1} − 2T_i)
```

At each outer loop tick (10 ms):
1. Capture endpoint-to-chain heat fluxes from the CURRENT (pre-integration) state.
2. Sub-step the intermediate chain nodes to stability.
3. Update endpoint temperatures using the captured fluxes.

This "split-step" coupling ensures energy conservation and that the chain
accurately stores/releases transient energy (the averaging mechanism).

**Example response for dist_water_to_sensor_mm = 10 mm, C_W = 42 J/°C:**

| Time | Block drop | Sensor drop | Ratio |
|---|---|---|---|
| 100 ms | 1.5 °C | 0.002 °C | 0.1 % (dead-time zone) |
| 200 ms | 3.0 °C | 0.035 °C | 1.2 % (wavefront arriving) |
| 500 ms | 7.1 °C | 0.6 °C | 8.5 % (chain still damping) |
| 1000 ms | 13.3 °C | 3.0 °C | 22 % (chain fully engaged) |
| 2000 ms | 23.5 °C | 10.6 °C | 45 % (approaching steady state) |

**Effect on PID control:**

- **`dist_water_to_sensor_mm`** — most impactful. Dead-time zone before sensor
  responds, then Gaussian-shaped rise. PID tuning is harder because integral
  term accumulates during the dead time, causing overshoot.

- **`dist_water_to_heater_mm`** — dead time in the heating path. Heater element
  overshoots thermally before block "feels" the energy. Tuning sluggish response.

- **`dist_sensor_to_heater_mm`** — direct heater→sensor shortcut. At high duty
  the sensor overshoots; at low duty it undershoots. Compounds the other effects.

**Runtime tuning:** All three distances are adjustable at runtime via HA
number entities (`"Mock Dist Water-Heater"`, `"Mock Dist Water-Sensor"`,
`"Mock Dist Sensor-Heater"`) without reflashing.


---

### Bypass Mode (Open Valve Pumping)

**What it models:** When pumping through an open valve (steam valve, purge
valve), there is no puck resistance. Water flows freely with minimal pressure
buildup — unlike brewing through a coffee puck which creates back-pressure.

**When it activates:**
- Steam PURGING state (pump + purge valve)
- Steam STEAMING state (pump + steam valve)
- Steam COOLING state (pump + purge valve for active cooling)
- Flush action (pump + purge valve)

**Model:**

```
D_eff = 1.0           # No puck resistance
flow = nominal_flow   # Maximum flow rate
pressure ≈ 0          # No back-pressure
```

**Implementation:** The orchestrator calls `IPump::set_bypass_mode(true)` when
entering these states, and `set_bypass_mode(false)` when returning to normal
operation or safe-stopping.

---

## Not-Covered Scenarios

### ❌ Thermal Runaway

**What it is:** The heater temperature increases without bound because the PID
output is stuck at 100% (e.g., sensor disconnected returns 0 °C, PID
computes huge positive error and drives heater full-on). In a real machine
this can boil the thermoblock dry, melt solder joints, or cause fire.

**Why not covered:** The mock heater's ODE always receives a valid temperature
reading from the same physics simulation. There is no way for the sensor to
report 0 °C while the block is actually at 200 °C in the current model.

**How to test manually:** Set `initial_temperature` very high and verify that
the `on_value_range` safety cutoff in the example YAML triggers correctly.

**C++ safety test plan:**

```
TEST: OverTemperatureCutoffFiredWhenSensorExceedsLimit
  - Inject temperature = 166 °C directly into orchestrator
  - Verify heater mode switches to OFF within one loop tick
  - Verify the safety flag is set and logged at ERROR level
  - STATUS: ✅ IMPLEMENTED — see Safety.OverTempCutoffFiredWhenSensorExceedsLimit

TEST: SensorFaultReturnsZeroAndPIDSaturates
  - Simulate sensor returning 0 °C while real temperature is high
  - Verify the hard cutoff limits PID output (requires sensor fault injection)
  - STATUS: NaN injection implemented — see Safety.SensorNaNForcesHeaterOff
```

### ❌ Puck Channeling

**What it is:** A crack or void in the coffee puck causes water to channel
through a low-resistance path instead of extracting evenly. The symptom is a
sudden step-increase in flow rate mid-shot, long before the puck is fully
extracted.

**Why not covered:** The wetting model uses a single lumped puck resistance
(`puck_pressure_bar`). It cannot model a spatial crack that only opens at a
specific time or volume.

**Approximate simulation:** Manually lower `puck_pressure_bar` part-way
through a shot using the HA number entity. This simulates the sudden drop
in resistance a channel would cause.

**C++ safety test plan:**

```
TEST: OrchestratorHandlesSuddenFlowSpike
  - Run brew sequence to pre-infusion phase
  - Inject step-increase in flow rate (simulate channel opening)
  - Verify brew terminates normally at flow_max despite faster-than-expected
    delivery (not a safety risk, but confirms no state-machine hang)
```

### ❌ Thermocouple / Sensor Failure

**What it is:** The MAX6675/MAX31855 returns a fault code, or the ADC reads
a stuck value (e.g., always 0, always 4095). The PID may drive the heater
to 100% duty because it thinks the temperature is far below setpoint.

**Why not covered:** The mock heater always exposes the physics-simulated
temperature; it cannot return NaN or a stuck fault value in the current API.

**C++ safety test plan:**

```
TEST: SafetyCutoffActivatesWhenPIDOutputLocksHigh
  - Set PID output to 1.0 (100% duty) for extended time
  - Verify temperature rises and triggers on_value_range cutoff at 165 °C
  - STATUS: requires orchestrator-level heater-fault injection

TEST: HeaterForcedOffWhenSensorReturnsNaN
  - Inject NaN from temperature sensor
  - Verify orchestrator disables heater within one loop tick
  - STATUS: ✅ IMPLEMENTED — see Safety_SensorNaNForcesHeaterOff in test_orchestrator.cpp
```

### ❌ Water Hammer / Pump Surge

**What it is:** A vibration pump produces pressure pulses at mains frequency
(50/60 Hz). At certain flow conditions these pulses can cause audible
"hammering" or pressure spikes that stress fittings. Not a safety risk for
control logic but can be a nuisance.

**Why not covered:** The mock pump uses a continuous-time ODE at 10 ms
resolution, which smooths out pulse-level dynamics.

---

## Safety Testing Plan (C++ Unit Tests)

The scenarios below target the **orchestrator** and **real hardware** code
paths — not the mock physics. They verify that the control logic never puts the
machine in a dangerous state, regardless of what the mock components return.

### Priority 1 — Thermal Safety

| Test ID | Scenario | Expected Outcome | Status |
|---|---|---|---|
| `Safety_OverTempCutoff` | Temperature exceeds 165 °C | Heater forced OFF immediately | ✅ Implemented |
| `Safety_PIDNotResumeAfterCutoff` | PID tries to re-enable heater post-cutoff | Cutoff interlock blocks it | ✅ Implemented |
| `Safety_HeaterOffOnReset` | ESP32 resets mid-brew | SSR defaults to LOW (heater off) | ✅ Hardware default (no test needed) |
| `Safety_WatchdogReboot` | Loop stalls > watchdog timeout | ESPHome resets; heater defaults off | ✅ ESPHome built-in |

### Priority 2 — Valve & Flow Safety

| Test ID | Scenario | Expected Outcome | Status |
|---|---|---|---|
| `Safety_ValveInterlockEnforced` | Two valves open simultaneously | Second valve refused / first closed | ✅ Implemented in valve platform tests |
| `Safety_BrewStopsAtFlowMax` | `flow_max` reached mid-shot | Brew sequence stops pump and closes valve | ✅ Implemented in orchestrator tests |
| `Safety_ResidualFlowAfterStop` | Residual pressure drains after pump off | Volume does not overflow `flow_max + margin` | ✅ Implemented (`Safety.ResidualFlowAfterStop` test) |
| `Safety_PurgeOnSteamStop` | Steam stopped by user | Purge valve opens; pressure safely released | ✅ Implemented in orchestrator tests |
| `Safety_PurgeBeforeSteam` | Steam sequence started with `purge_volume > 0` | Purge valve open + pump on during PURGING; steam valve only opens after purge | ✅ Implemented (`SteamPurge.*` tests) |
| `Safety_SteamTimeout` | Steaming runs past `timeout` duration | STEAMING auto-stops, enters COOLING | ✅ Implemented (`SteamTimeout.*` tests) |
| `Safety_BrewTemperatureCooldown` | `temperature_cooldown: true` and thermoblock above target when `brew_start()` is called (e.g. after stopped steam at 110°C) | Pre-brew COOLING state entered; purge valve open, pump on, heater setpoint lowered; transitions to HEATING when temp ≤ target | ✅ Implemented (`BrewTemperatureCooldown.*` tests) |

### Priority 3 — Grinder Independence

| Test ID | Scenario | Expected Outcome | Status |
|---|---|---|---|
| ~~`Safety_GrinderLockedDuringBrew`~~ | ~~Grinder triggered while brew active~~ | ~~Grinder refused~~ | ✅ N/A — **Design decision:** Grinder and brew are independent operations |
| ~~`Safety_GrinderLockedDuringSteam`~~ | ~~Grinder triggered while steam active~~ | ~~Grinder refused~~ | ✅ N/A — **Design decision:** Grinder and steam are independent operations |

### Priority 4 — Sensor Fault Handling

| Test ID | Scenario | Expected Outcome | Status |
|---|---|---|---|
| `Safety_SensorNaNForcesHeaterOff` | Temperature sensor returns NaN | Heater forced OFF via `IHeater::force_off()`; state machine safe-stopped | ✅ Implemented |
| `Safety_SensorStuckZeroActivatesCutoff` | Temperature sensor returns 0 °C | PID drives to 100% but cutoff fires at 165 °C | ⚠️ Covered by over-temp cutoff |
| `Safety_FlowSensorStuckZero` | Flow sensor returns 0 indefinitely | Brew times out after `brew_timeout_ms` | ✅ Implemented (P0-4) |

### Priority 5 — Scale / Weight-Based Exit (Planned — Phase 13)

| Test ID | Scenario | Expected Outcome | Status |
|---|---|---|---|
| `Scale_BrewExitsAtTargetWeight` | Cup weight reaches `target_weight` during BREWING | `brew_stop()` called; pump and valve closed | ⬜ Planned |
| `Scale_StaleScaleFallsBackToFlowMax` | Scale reading stale during BREWING | Brew continues using `flow_max` volumetric exit | ⬜ Planned |
| `Scale_TaredOnBrewStart` | `brew_start()` called with prior weight on scale | Scale weight resets to 0 before pump activates | ⬜ Planned |
| `Scale_GrinderStopsAtTargetDose` | Portafilter weight reaches `target_dose` | Grinder motor stops immediately | ⬜ Planned |
| `Scale_GrinderFallsBackToTimedGrind` | Scale stale during grind | Grinder uses `default_grind_time` as fallback | ⬜ Planned |
| `Scale_TaredOnGrindStart` | Grind started with prior dose weight on scale | Scale weight resets to 0 before motor activates | ⬜ Planned |

---

## How to Use the Residual Pressure Model for Failure Testing

The `internal_volume_ml` parameter lets you reproduce pressure-driven
over-extraction scenarios without physical hardware:

```yaml
espresso_machine_mock_pump:
  nominal_flow_ml_per_s: 4.0
  internal_volume_ml: 50.0   # Large volume → very slow pressure bleed-off
```

With `internal_volume_ml = 50` and `nominal_flow = 4`:
- τ_decay = 12.5 s
- After pump stops, flow is still at ~37% of Q_ss after 12.5 s
- Total extra volume ≈ Q_ss × τ × (1 − e⁻⁵) ≈ 4 × 12.5 × 0.993 ≈ 50 mL

This lets you test whether the brew valve closure (or purge valve opening)
terminates the shot before over-extraction occurs.

You can also adjust `internal_volume_ml` at runtime via the
`"Mock Internal Volume"` HA number entity without reflashing.

---

## Mock Scale — `espresso_machine_mock_scale`

> **Status: Planned — Phase 13b.  No code written yet.**
> See `docs/scales.md` for the full mock scale architecture.

The `espresso_machine_mock_scale` component is a software-only simulation of the
`IScale` interface.  It lets you develop and test weight-based brew exit and grinder
dosing without any physical scale hardware, following the same pattern as
`espresso_machine_mock_heater` and `espresso_machine_mock_pump`.

### Cup scale mode (brew): weight from pump nozzle output

The cup scale reads `MockPump::get_nozzle_flow_total()` and multiplies by
`liquid_density_g_per_ml` (default 1.05 g/mL for espresso) to produce a simulated
cup weight.  The pump's nozzle model already accounts for puck water absorption, so
the nozzle total represents the actual espresso yield in the cup.

```
weight_g(t) = nozzle_total_ml(t) × liquid_density_g_per_ml
flow_g_s(t) = nozzle_rate_ml_s(t) × liquid_density_g_per_ml
```

**Simulated timeline (puck_density=50, target_weight=36 g):**

```
t=0 s   brew_start() → tare (weight = 0)
t=1 s   pump starts; puck absorbing most water; nozzle_total ≈ 0.3 mL → weight ≈ 0.3 g
t=10 s  puck ~50% saturated; nozzle_total ≈ 8 mL → weight ≈ 8.4 g
t=20 s  puck ~80% saturated; nozzle_total ≈ 22 mL → weight ≈ 23 g
t=27 s  nozzle_total ≈ 34 mL → weight ≈ 36 g → brew_stop() triggered
```

### Portafilter scale mode (grinder): dose by speed

The portafilter scale accumulates weight at `dose_rate_g_per_s` while the grinder
relay is active:

```
weight_g(t) = dose_rate_g_per_s × grinder_active_time_s
```

Weight is reset (tared) automatically at grind start.  Grind stops when
`weight_g >= target_dose` or `dose_timeout` elapses.

**Simulated timeline (dose_rate=2 g/s, target_dose=18 g):**

```
t=0 s   grind_start() → tare (weight = 0)
t=3 s   weight = 6 g
t=6 s   weight = 12 g
t=9 s   weight = 18 g → grinder stops
```

### Simulating a stale scale (fallback path)

Set `dose_rate_g_per_s: 0` to simulate a scale that produces no weight signal
(or disconnect the `mock_pump:` reference for the cup scale).  The orchestrator
falls back to `flow_max` for brew and `default_grind_time` for the grinder —
exactly the paths that need to be validated.

```yaml
espresso_machine_mock_scale:
  id: cup_scale
  mock_pump: main_pump
  dose_rate_g_per_s: 0.0   # simulate stale/disconnected scale → triggers flow_max fallback
```

### How to use the mock scale for failure testing

| Test | Setup | Expected result |
|---|---|---|
| Brew exits at target weight | `target_weight: 36g`; `liquid_density: 1.05` | Brew stops when nozzle × density ≥ 36 g |
| Stale scale → flow_max exit | `dose_rate: 0` or `mock_pump: none` | Brew exits at `flow_max` volumetric limit |
| Tare verified on brew start | Check weight=0 at BREWING entry | `IScale::tare()` called by orchestrator before pump |
| Grinder dose exit | `target_dose: 18g`; `dose_rate: 2.0` | Grinder stops at 9 s (18 g ÷ 2 g/s) |
| Stale scale → timed grind | `dose_rate: 0` | Grinder runs for `default_grind_time` then stops |
| Tare verified on grind start | Check weight=0 at grind entry | `IScale::tare()` called by grinder before motor |
