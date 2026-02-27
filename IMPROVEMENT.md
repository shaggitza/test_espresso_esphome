# Architecture & Code Review — Improvement Suggestions

> **Scope:** Full review of all C++ components, Python schemas, YAML examples,
> and documentation.  Issues are grouped by category and assigned a severity
> level: 🔴 **Critical** (safety / correctness), 🟠 **Major** (significant
> logic or reliability concern), 🟡 **Minor** (code quality / maintainability),
> 🔵 **Documentation** (doc/code discrepancy or missing information).

---

## Table of Contents

1. [Safety Issues](#1-safety-issues)
2. [Logic and Business-Logic Issues](#2-logic-and-business-logic-issues)
3. [Architecture and Code-Quality Issues](#3-architecture-and-code-quality-issues)
4. [Documentation Discrepancies](#4-documentation-discrepancies)
5. [Summary Table](#5-summary-table)

---

## 1. Safety Issues

### 1.1 🔴 Emergency stop blocked by `min_on_ms_` enforcement

**File:** `components/espresso_machine_pump/pump.cpp` → `PumpSwitch::write_state()`

**Problem:**
`safe_stop_all_()` calls `brew_pump_->turn_off()` (and `steam_pump_->turn_off()`),
which routes through `PumpSwitch::write_state(false)`. Inside that method the
minimum-on-time guard fires:

```cpp
if (min_on_ms_ > 0 && (millis() - last_on_ms_) < min_on_ms_)
  return;  // ← silently ignores the turn-off request
```

With the default `min_on_ms_ = 500 ms`, an emergency over-temperature cutoff or a
user-triggered `brew_stop()` called within 500 ms of the pump starting will **not
immediately stop the pump**. This is a safety gap — safety-driven stops must bypass
the timing guard.

**Recommendation:**
Add a `force_off()` method to `IPump` (and `PumpSwitch`) that skips the timing
constraints and always stops the pump immediately. Use `force_off()` inside
`safe_stop_all_()` instead of `turn_off()`.

---

### 1.2 🔴 Steam COOLING without a heater controller: no temperature gate

**File:** `components/espresso_machine/espresso_machine.cpp` → `advance_steam_()`

**Problem:**
In `SteamState::COOLING`, the code waits for
`steam_heater_ctrl_->get_current_temperature() <= steam_cool_down_to_`. However,
when `steam_heater_ctrl_` is `nullptr`, this check is skipped entirely:

```cpp
if (steam_heater_ctrl_) {
  if (steam_heater_ctrl_->get_current_temperature() > steam_cool_down_to_) {
    break;  // Still cooling
  }
}
// Falls through to CLEANUP immediately when no controller is wired
```

A machine running without `heater_controller:` can exit the COOLING state
immediately after `steam_stop()` and transition to IDLE while the thermoblock is
still at ~135 °C. If the user then opens the steam valve manually from HA, scalding
steam is present without any protection.

**Recommendation:**
Document prominently that `heater_controller:` is **required** for safe steam
operation. Consider making `steam.heater_controller:` a required field when steam is
configured, or at minimum emit a compile-time `ESP_LOGW` / schema warning when it is
absent.

---

### 1.3 🔴 Over-temperature cutoff only disables one IHeater

**File:** `components/espresso_machine/espresso_machine.cpp` → `check_over_temp_safety_()`

**Problem:**
When the cutoff fires, only `over_temp_sensor_->force_off()` is called. If the user
has configured a setup where `brew_heater_ctrl_` or `steam_heater_ctrl_` is a
*different* `IHeater` object from `over_temp_sensor_` (theoretically possible, e.g.
two separate mock heaters in a test rig), the second heater is not disabled.

```cpp
over_temp_sensor_->force_off();  // Only this heater is shut off
// brew_heater_ctrl_ and steam_heater_ctrl_ are NOT touched
```

In the reference hardware there is a single heater shared across all three roles,
so this is not a day-to-day concern, but the architecture does not enforce this
assumption and could mislead future contributors.

**Recommendation:**
In `check_over_temp_safety_()`, call `force_off()` on all distinct `IHeater`
pointers the orchestrator holds:

```cpp
if (brew_heater_ctrl_)   brew_heater_ctrl_->force_off();
if (steam_heater_ctrl_)  steam_heater_ctrl_->force_off();
if (over_temp_sensor_ && over_temp_sensor_ != brew_heater_ctrl_ &&
    over_temp_sensor_ != steam_heater_ctrl_)
  over_temp_sensor_->force_off();
```

---

### 1.4 🟠 ISR race condition in flow meter reset

**File:** `components/espresso_machine_flow_meter/flow_meter.cpp` → `reset()` and `loop()`

**Problem:**
`pulse_count_` is declared `volatile uint32_t`. On Xtensa LX6 (ESP32), aligned
32-bit reads and writes happen to be atomic at the hardware level, so the current
code works in practice. However, `reset()` does:

```cpp
pulse_count_ = 0;
last_pulse_count_ = 0;
```

Between these two assignments the ISR could fire and increment `pulse_count_` to 1.
`loop()` then computes `delta = 1 − 0 = 1`, crediting one ghost pulse. Over
thousands of shots this accumulates into a small but real calibration drift.

Similarly, `calibrate()` reads `pulse_count_` without suspending interrupts.

**Recommendation:**
Wrap the affected read/write sequences with `portENTER_CRITICAL_ISR` / `portEXIT_CRITICAL_ISR`
(or `noInterrupts()` / `interrupts()` for the Arduino framework) to guarantee
atomicity. This also documents the intent clearly for future maintainers.

---

### 1.5 🟠 `flush()` with null pump enters permanent FLUSHING state

**File:** `components/espresso_machine/espresso_machine.cpp` → `flush()` and `advance_flush_()`

**Problem:**
`flush()` can be invoked without a brew section configured (i.e. `brew_pump_` is
`nullptr`). The function sets `mode_ = EspressoMode::FLUSHING` and then
`advance_flush_()` reads:

```cpp
float pumped = brew_pump_ ? brew_pump_->get_flow_total() : 0.0f;
if (pumped >= flush_volume_ml_) { ... }
```

If `brew_pump_` is null, `pumped` is always 0 and the condition `0 >= flush_volume_ml_`
(where `flush_volume_ml_ > 0`) is always false. The machine is permanently stuck in
`FLUSHING` mode with no way out short of a reboot.

**Recommendation:**
Guard at the start of `flush()`:

```cpp
if (brew_pump_ == nullptr || brew_purge_valve_ == nullptr) {
  ESP_LOGW(TAG, "flush ignored: brew pump or purge valve not configured");
  return;
}
```

---

### 1.6 🟠 Brew timeout starts from `brew_start()`, not from when brewing begins

**File:** `components/espresso_machine/espresso_machine.cpp` → `advance_brew_()` (BREWING case)

**Problem:**
`brew_start_ms_` is set in `brew_start()`. The brew timeout check in the BREWING state
compares `millis() - brew_start_ms_`. If the machine spends a significant period in
the COOLING state (cooling from 135 °C to 90 °C can take 3–8 minutes on a single
thermoblock), the timeout clock is running the entire time. This can cause the brew
timeout to fire before the puck has received any water, producing a confusing and
frustrating user experience.

**Recommendation:**
Reset `brew_start_ms_` (or use a separate `brew_shot_start_ms_`) when entering
`BrewState::BREWING` — not in `brew_start()`. The BREWING state already tracks
`brew_shot_start_ms_` for shot-time recording; reuse that variable for the timeout
check.

---

## 2. Logic and Business-Logic Issues

### 2.1 🟠 Negative shot yield not guarded

**File:** `components/espresso_machine/espresso_machine.h` → `get_last_shot_yield_ml()`

**Problem:**

```cpp
float get_last_shot_yield_ml() const {
  return last_shot_volume_ml_ > 0.0f ? last_shot_volume_ml_ - brew_flow_offset_ml_ : 0.0f;
}
```

When a shot is stopped manually very early (e.g. 5 ml extracted but `brew_flow_offset_ml_`
is 20 ml), the result is `-15.0 ml`. This negative value gets published to the HA
`last_shot_yield` sensor, which confuses automations and dashboards that might use
the value to trigger scripts or display it to the user.

**Recommendation:**

```cpp
float raw = last_shot_volume_ml_ - brew_flow_offset_ml_;
return raw > 0.0f ? raw : 0.0f;
```

---

### 2.2 🟠 Temperature-surfing setpoint written on every `loop()` tick

**File:** `components/espresso_machine/espresso_machine.cpp` → `advance_brew_()` (BREWING case)

**Problem:**
During BREWING, `brew_heater_ctrl_->set_target_temperature(desired_temp)` is called
**every loop iteration** (~10–50 ms). `EspressoMachineHeater::set_target_temperature()`
constructs and performs a `ClimateCall` on every tick:

```cpp
void set_target_temperature(float t) override {
  auto call = climate_->make_call();
  call.set_target_temperature(t);
  call.perform();  // heavy: triggers ESPHome state machine and HA publish
}
```

This causes unnecessary HA traffic, ESPHome internal state updates, and PID
disturbance every loop cycle — even when the ramp has already completed and
`desired_temp == brew_target_temp_`.

**Recommendation:**
Cache the last setpoint sent and only call `set_target_temperature()` when the value
changes by more than a small threshold (e.g. 0.1 °C):

```cpp
if (std::abs(desired_temp - last_surf_setpoint_) > 0.1f) {
  brew_heater_ctrl_->set_target_temperature(desired_temp);
  last_surf_setpoint_ = desired_temp;
}
```

---

### 2.3 🟡 `BrewFlowMaxNumber`: no C++-side bounds validation

**File:** `components/espresso_machine/espresso_machine.cpp` → `BrewFlowMaxNumber::control()`

**Problem:**
The Python schema limits the number entity to min=10, max=200 mL. But a direct Home
Assistant service call (e.g. via developer tools) can write an arbitrary float
without ESPHome schema validation. The C++ control handler calls
`parent_->set_brew_flow_max(value)` unconditionally. A value of 0 or negative would
cause the shot to terminate immediately on the first flow tick (since `0 >= 0` is
true), effectively disabling volumetric control.

**Recommendation:**
Add a guard in `BrewFlowMaxNumber::control()`:

```cpp
if (value < 10.0f) {
  ESP_LOGW(TAG, "Brew flow max %.1f mL ignored — must be >= 10 mL", value);
  return;
}
```

---

### 2.4 🟡 `PumpNumber` (dimmer type) missing flow-rate interface methods

**File:** `components/espresso_machine_pump/pump.h` → `PumpNumber`

**Problem:**
`PumpNumber` inherits from `IPump` via the base `number::Number` path, but it does
not override `get_flow_rate()`, `get_flow_total()`, `reset_flow()`, or
`set_target_flow()`. If a user configures `type: dimmer` and wires it as the steam
pump, the orchestrator calls `steam_pump_->set_target_flow(steam_flow_max_ml_per_s_)`
— which silently does nothing (the default no-op in `IPump`). Bang-bang flow control
will not work and the steam pump will run continuously with no modulation.

**Recommendation:**
Either:
- Document prominently that `type: dimmer` does **not** support flow-rate control and
  cannot be used as the steam pump with non-zero `flow_max`.
- Or add a schema validation error in Python that rejects `type: dimmer` when the
  pump is used as a steam pump.

---

### 2.5 🟡 Grinder: milliseconds stored as `uint32_t`, published as `float` seconds

**File:** `components/espresso_machine_grinder/grinder.h` and `grinder.cpp`

**Problem:**
`default_grind_time_ms_` is stored in milliseconds as `uint32_t`. The
`GrinderTimeNumber` entity exposes it via `publish_state(static_cast<float>(default_grind_time_ms_))`
without any unit conversion, implying the HA entity value is in milliseconds.
This is non-obvious and inconsistent with ESPHome convention where `number` entities
typically use human-scale units (seconds for time).

**Recommendation:**
Store grind time in seconds internally as a `float`, or clearly annotate the number
entity with `unit_of_measurement: "ms"` in the Python schema and update the
documentation to clarify the unit.

---

### 2.6 🟡 `advance_brew_()` DONE state: cleanup function called on every loop tick

**File:** `components/espresso_machine/espresso_machine.cpp` → `advance_brew_()` (DONE case)

**Problem:**
The `DONE` case is:

```cpp
case BrewState::DONE:
  if (brew_cleanup_fn_)
    brew_cleanup_fn_();
  brew_state_ = BrewState::CLEANUP;
  ...
```

`advance_brew_()` is called once per `loop()`, so `brew_cleanup_fn_()` is called
exactly once. However, if `advance_brew_()` were ever called twice in the same tick
(e.g. through a future refactor), the cleanup script would fire twice. More
importantly, the state immediately transitions to CLEANUP, so there is no actual
issue today — but the DONE→CLEANUP transition in a single tick is non-obvious and
is missing a `publish_status_()` call between DONE and CLEANUP visible to the user.

**Recommendation:**
Add a comment explaining the immediate single-tick transition and confirm it is
intentional. The CLEANUP→IDLE transition is also a single tick, so users see
"Done → IDLE" with no intermediate CLEANUP status flash — which is fine, but
should be documented.

---

## 3. Architecture and Code-Quality Issues

### 3.1 🟠 Valve interlock uses a process-wide singleton registry

**File:** `components/espresso_machine_valve/valve.cpp`

**Problem:**
`all_valves_()` returns a static local `std::vector<Valve *>` — a process-wide
singleton. Every `Valve` instance globally interlocks with every other `Valve`
instance on the same firmware image, regardless of which orchestrator owns them.

While today there is a single `EspressoMachine` orchestrator, future multi-zone
setups (e.g. dual-boiler machine with two independent brew groups, or a development
unit with two test harnesses) would share the interlock unintentionally.

In unit tests, the `reset_registry()` call must be made between tests to avoid
cross-test contamination, creating a hidden test-order dependency.

**Recommendation:**
Consider scoping the registry per-orchestrator (pass an orchestrator ID or pointer),
or at minimum document the singleton behaviour prominently in the class header. The
test infrastructure should call `Valve::reset_registry()` unconditionally in every
test fixture's `SetUp()`.

---

### 3.2 🟡 `MockHeater` has two `protected:` access specifiers

**File:** `components/espresso_machine_mock_heater/mock_heater.h`

**Problem:**
The `MockHeater` class has two separate `protected:` sections (lines ~179 and ~204).
In C++ this is syntactically legal but misleading — readers expect a single
contiguous protected block. This is a minor code-quality smell.

**Recommendation:**
Merge the two `protected:` sections into one.

---

### 3.3 🟡 Brew and steam share the same purge valve — interlock silently closes it

**File:** `examples/philips_barista_brew.yaml`, `examples/philips_barista_brew_mock.yaml`

**Problem:**
In the example YAML, both `brew.purge_valve` and `steam.purge_valve` reference the
same `purge_valve` entity. The valve interlock closes all other valves when any valve
opens. If HA automations open the brew purge valve while a steam sequence is in the
COOLING state (where the steam purge valve is supposed to be open), the interlock
will close the steam purge path — interrupting the pressure-relief sequence.

This is only an issue when HA automations or the `espresso_machine.flush` action
interact with valves while the steam COOLING sequence is running.

**Recommendation:**
Add an explicit note in the YAML example and `docs/wiring.md` explaining that the
shared purge valve means no manual valve operations should be triggered while the
steam COOLING/CLEANUP sequence is running. Consider checking `mode_` in the valve's
`open()` method to reject manual opens when a safety sequence is active.

---

### 3.4 🟡 `status_name()` stack buffer close to limit for future expansions

**File:** `components/espresso_machine/espresso_machine.cpp` → `status_name()`

**Problem:**
A 128-byte stack buffer (`char buf[STATUS_BUF_SIZE]`) is used to format the status
string. Current strings are well within the limit. However, if future states add
longer messages (e.g. including shot number, profile name, or dual temperature
readings), the buffer could overflow silently on the device. The existing truncation
check only logs a warning after the fact:

```cpp
if (n < 0 || static_cast<size_t>(n) >= STATUS_BUF_SIZE)
  ESP_LOGW(TAG, "status_name: output truncated (%d bytes)", n);
```

A truncated status string is returned to HA rather than an error — the user sees
partial data.

**Recommendation:**
Increase the buffer to 192 or 256 bytes, or switch to `std::string` with
`std::ostringstream` / `snprintf` into a dynamically sized string. Also consider
returning a sentinel string like `"Status error"` when truncation is detected.

---

### 3.5 🟡 `flush()` silently proceeds when brew section is unconfigured

**File:** `components/espresso_machine/espresso_machine.cpp` → `flush()`

**Problem:**
The `espresso_machine.flush` action only requires the `espresso_machine` component
to be configured — it does not require a `brew:` section. If a user configures only
`steam:` and calls flush, `brew_pump_` and `brew_purge_valve_` are null. The pump
operations are null-guarded inside `advance_flush_()` so no crash occurs, but the
machine enters `FLUSHING` mode and never exits (see §1.5).

**Recommendation:**
(See §1.5 for the fix.) Additionally, in the Python schema, the `flush` action
should check that the component's brew section is configured or add explicit
documentation that flush requires brew hardware.

---

## 4. Documentation Discrepancies

### 4.1 🔵 README shows `pump_min_on_time` inside `espresso_machine.steam:` — wrong schema section

**Files:** `README.md` (line ~207), `docs/.github/copilot-instructions.md`

**Problem:**
Both files show this YAML snippet:

```yaml
espresso_machine:
  steam:
    ...
    pump_min_on_time: 2s   # minimum pump on-time enforced by pump hardware (P2-7)
```

`pump_min_on_time` is **not** a valid key in `STEAM_SCHEMA` (`espresso_machine/__init__.py`).
It is a valid key in `espresso_machine_pump:` (the pump component schema). Using this
snippet verbatim will produce an ESPHome validation error:

```
Invalid config for [espresso_machine.steam]: extra keys not allowed @ data['steam']['pump_min_on_time']
```

**Recommendation:**
Remove `pump_min_on_time` from the steam YAML snippet in `README.md` and add a comment
explaining that the pump's minimum on-time is set in the `espresso_machine_pump:` block.

---

### 4.2 🔵 FEATURES.md: `pump_min_on_time` described as being "in steam schema"

**File:** `FEATURES.md` (Steam Mode table)

**Problem:**

> Steam pump minimum on-window (`pump_min_on_time`) | ✅ | … configurable via `pump_min_on_time:` in steam schema (P2-7)

The `pump_min_on_time` key is in `espresso_machine_pump` (the pump component),
not in the orchestrator's `steam:` sub-schema. The note "in steam schema" is
incorrect and will send users looking in the wrong place.

**Recommendation:**
Update the note to: "configurable via `pump_min_on_time:` in the
`espresso_machine_pump:` component block."

---

### 4.3 🔵 README vs. FEATURES.md: Grinder lockout status is contradictory

**Files:** `README.md` (Features table), `FEATURES.md` (Safety Interlocks table)

**Problem:**

- `README.md`: "**Grinder integration** | ✅ Implemented | Timed relay grind;
  adjustable from HA; brew/steam lockout ⬜ pending"
- `FEATURES.md`: "Grinder independence | ✅ | **Design decision:** Grinder and
  brew/steam are independent operations; no lockout needed"

These are directly contradictory. README implies lockout is planned but unimplemented.
FEATURES.md documents a deliberate architectural decision that lockout is not needed.

**Recommendation:**
Update `README.md` to remove "brew/steam lockout ⬜ pending" and replace it with
"no brew/steam lockout by design — grinder is an independent entity."

---

### 4.4 🔵 Example YAML implementation-status comment out of date

**File:** `examples/philips_barista_brew.yaml` (file header, lines ~11–14)

**Problem:**
The header comment lists:

```yaml
#   🚧 Partial:      temperature surfing (config accepted; ramp applies via IHeater),
#                    hard over-temperature cutoff (C++ + on_value_range)
```

Both features are marked ✅ fully implemented in `FEATURES.md`. The partial status
comment should have been updated when these features were completed.

**Recommendation:**
Move both items to the "✅ Implemented" list in the header comment.

---

### 4.5 🔵 `docs/failure_scenarios.md`: Does not cover physical sensor/actuator failures

**File:** `docs/failure_scenarios.md`

**Problem:**
The document covers software/state-machine failure modes thoroughly but omits
common hardware failure scenarios:

- **Flow meter disconnected / 0 pulses indefinitely:** Shot never reaches `flow_max`,
  runs until brew timeout fires. If brew timeout is disabled (default), the shot
  runs indefinitely.
- **Pump runs but no flow (cavitation / air lock):** Similar outcome — flow meter
  returns 0, no auto-stop.
- **Valve stuck open:** Machine enters next state with wrong hydraulic state.
- **Valve stuck closed:** Brew path blocked — pump can overpressure.

**Recommendation:**
Add a "Hardware Fault Scenarios" section to `failure_scenarios.md` covering the
above cases and their firmware mitigation (e.g. brew timeout as the last-resort
stop for stuck flow meter, over-pressure detection via pressure transducer in a
future phase).

---

### 4.6 🔵 `docs/wiring.md`: No guidance on galvanic isolation or grounding

**File:** `docs/wiring.md`

**Problem:**
The guide correctly notes that the 220 V side must remain isolated and recommends
opto-isolated SSRs, but provides no concrete guidance on:

- Whether the machine's original LV GND and HV GND share a reference.
- How to verify galvanic isolation before power-on.
- Ground loop risks when the ESP32 is powered via USB from a computer while the HV
  board is live.

**Recommendation:**
Add a short "Safety Check" subsection to `docs/wiring.md` covering isolation
verification (multimeter continuity test between HV chassis and LV GND), USB
power caveats, and referencing the Philips Barista Brew schematic if available.

---

### 4.7 🔵 `structure.md` missing the `espresso_machine_heater` component

**File:** `structure.md`

**Problem:**
The file documents the repository layout but does not mention
`components/espresso_machine_heater/` — the production `IHeater` adapter introduced
in Phase 1. Any developer reading `structure.md` to orient themselves will miss this
component.

**Recommendation:**
Add a row for `espresso_machine_heater` to the component table in `structure.md`.

---

### 4.8 🔵 `FEATURES.md` platform table missing `espresso_machine_heater` entry

**File:** `FEATURES.md` (Platform Components table)

**Problem:**
`espresso_machine_heater` appears in the "Notes" column of several rows but has no
dedicated entry in the Platform Components table at the top of the file.

**Recommendation:**
Add a row:

```
| `espresso_machine_heater` | `component` | ✅ | Production IHeater adapter wrapping `climate::Climate`; temperature tolerance configurable |
```

(This row already exists further down in the Orchestrator Features table but is
absent from the top Platform Components table.)

---

### 4.9 🔵 `heater_controller:` not marked required for safe steam operation in any schema

**File:** `components/espresso_machine/__init__.py` (STEAM_SCHEMA)

**Problem:**
`heater_controller:` is `cv.Optional` in the steam schema, yet without it the
machine cannot temperature-gate the HEATING→STEAMING transition, cannot gate the
COOLING→CLEANUP transition on actual temperature, and may expose users to hot steam
before the thermoblock reaches target temperature.

The documentation notes this but it is easy to overlook.

**Recommendation:**
Add a schema-level warning using `cv.All()` with a validator that emits an
`ESP_LOGW` at component registration time (or a Python `raise cv.Invalid`) when
`steam:` is configured without `heater_controller:`. At minimum, document this in
the YAML example with a prominent comment:

```yaml
  steam:
    heater_controller: brew_heater_ctrl  # STRONGLY RECOMMENDED — see docs/failure_scenarios.md
```

---

### 4.10 🔵 `copilot-instructions.md` YAML snippet matches README error

**File:** `.github/copilot-instructions.md`

**Problem:**
The same YAML snippet with `pump_min_on_time: 2s` inside `steam:` appears in the
copilot instructions (the instructions that guide future agent contributions to
this repository). This means future AI-assisted contributions will continue to
generate incorrect YAML snippets.

**Recommendation:**
Fix the YAML snippet in `copilot-instructions.md` as described in §4.1.

---

## 5. Summary Table

| # | Severity | Component | Issue |
|---|---|---|---|
| 1.1 | 🔴 Critical | `pump.cpp` | Emergency stop blocked by `min_on_ms_` timing guard |
| 1.2 | 🔴 Critical | `espresso_machine.cpp` | Steam COOLING exits immediately without temperature gate when no `heater_controller` wired |
| 1.3 | 🔴 Critical | `espresso_machine.cpp` | Over-temperature cutoff only disables `over_temp_sensor_`; `brew_heater_ctrl_` / `steam_heater_ctrl_` are not forcibly shut off |
| 1.4 | 🟠 Major | `flow_meter.cpp` | Non-atomic ISR counter operations in `reset()` and `calibrate()` cause calibration drift |
| 1.5 | 🟠 Major | `espresso_machine.cpp` | Null `brew_pump_` in `flush()` causes permanent FLUSHING state |
| 1.6 | 🟠 Major | `espresso_machine.cpp` | Brew timeout measured from `brew_start()` not from `BrewState::BREWING` entry — fires during COOLING or HEATING |
| 2.1 | 🟠 Major | `espresso_machine.h` | `get_last_shot_yield_ml()` can return a negative value |
| 2.2 | 🟠 Major | `espresso_machine.cpp` | Temperature-surfing setpoint written to heater every loop tick — excessive HA traffic |
| 2.3 | 🟡 Minor | `espresso_machine.cpp` | `BrewFlowMaxNumber::control()` missing C++-side bounds validation |
| 2.4 | 🟡 Minor | `pump.h` | `PumpNumber` (dimmer) missing flow-rate interface — bang-bang flow control silently doesn't work |
| 2.5 | 🟡 Minor | `grinder.h/.cpp` | Grind time stored in ms but published as float without explicit unit annotation |
| 2.6 | 🟡 Minor | `espresso_machine.cpp` | DONE→CLEANUP is a single-tick silent transition; no status flash to user |
| 3.1 | 🟠 Major | `valve.cpp` | Process-wide valve singleton registry causes cross-test contamination and is fragile in multi-zone setups |
| 3.2 | 🟡 Minor | `mock_heater.h` | Two `protected:` sections in `MockHeater` class — redundant, misleading |
| 3.3 | 🟡 Minor | YAML examples | Shared purge valve can be silently closed by interlock during steam COOLING |
| 3.4 | 🟡 Minor | `espresso_machine.cpp` | 128-byte `status_name()` stack buffer leaves little headroom for future message expansion |
| 3.5 | 🟡 Minor | `espresso_machine.cpp` | `flush()` with brew section unconfigured silently enters permanent FLUSHING state |
| 4.1 | 🔵 Doc | `README.md` | `pump_min_on_time` incorrectly shown inside `steam:` — rejects at ESPHome validation |
| 4.2 | 🔵 Doc | `FEATURES.md` | Pump min-on-time described as "in steam schema" — actually in pump component schema |
| 4.3 | 🔵 Doc | `README.md` vs `FEATURES.md` | Grinder lockout described as "⬜ pending" in README but "design decision: not needed" in FEATURES.md |
| 4.4 | 🔵 Doc | `examples/philips_barista_brew.yaml` | Header comment still shows temperature surfing and over-temp cutoff as 🚧 Partial |
| 4.5 | 🔵 Doc | `docs/failure_scenarios.md` | No coverage of hardware fault scenarios (stuck flow meter, stuck valve, pump cavitation) |
| 4.6 | 🔵 Doc | `docs/wiring.md` | No guidance on galvanic isolation verification or USB-powered debugging safety |
| 4.7 | 🔵 Doc | `structure.md` | `espresso_machine_heater` component absent from repository layout table |
| 4.8 | 🔵 Doc | `FEATURES.md` | `espresso_machine_heater` absent from the Platform Components table |
| 4.9 | 🔵 Doc | `__init__.py` (steam schema) | `heater_controller:` not flagged as strongly recommended for safe steam operation |
| 4.10 | 🔵 Doc | `copilot-instructions.md` | Same wrong `pump_min_on_time`-in-steam-section YAML snippet as README.md |

---

*Review conducted against the state of the repository as of the branch
`copilot/code-review-and-improvements`. All file/line references are indicative;
please re-verify against the current commit before applying fixes.*
