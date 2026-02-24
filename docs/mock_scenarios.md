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
| Flow-based thermoblock cooling | mock_heater | ✅ Full | IFlowObserver coupling; Cp × ΔT term in ODE |
| Thermoblock thermal mass | mock_heater | ✅ Full | Al block + water thermal mass |
| Ambient heat loss | mock_heater | ✅ Full | Newton cooling term h × (T − T_amb) |
| PID heater control | native ESPHome PID | ✅ Full | Unchanged from real config |
| Temperature overshoot at startup | mock_heater | ✅ Full | Controlled by thermal mass and PID gains |
| Over-temperature safety cutoff | mock_heater + YAML | ✅ Full | `on_value_range` above 165 °C in example YAML |
| Grinder lockout during brew | orchestrator | ✅ Full | State-machine interlock |
| Valve interlock (one open at a time) | valve platform | ✅ Full | Platform-level enforcement |
| Shot auto-termination at `flow_max` | orchestrator | ✅ Full | State machine checks `get_flow_total()` |
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
