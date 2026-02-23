# Failure Scenarios — ESPHome Espresso Machine Controller

This document catalogues safety-relevant scenarios that can arise during
machine operation, with particular emphasis on the **power on/off toggle**
and the interactions between on/off state and active brew/steam sequences.

For each scenario the document records:
- What can happen
- What the firmware does to handle it
- Test / coverage status

---

## On/Off Toggle — Scenario Matrix

| Scenario | Expected Behaviour | Status |
|---|---|---|
| Power ON while idle | Heater starts; brew/steam become available | ✅ Covered (C++ + YAML) |
| Power OFF while idle | Flag cleared; hardware already safe | ✅ Covered (C++ test) |
| Power OFF during brew | Brew stops immediately; pump off, valves closed | ✅ Covered (C++ test) |
| Power OFF during steam heat-up | Heat-up cancelled; heater lowered; all off | ✅ Covered (C++ test) |
| Power OFF during active steaming | Purge sequence initiated; completes automatically | ✅ Covered (C++ test) |
| Power OFF during steam cool-down | Cool-down + purge continues until IDLE | ✅ Covered (C++ test) |
| Power ON mid-purge (quick toggle after steam) | On flag set; new operations blocked until IDLE | ✅ Covered (C++ test) |
| Brew/steam start while machine is OFF | Ignored with warning log | ✅ Covered (C++ test) |
| Power cycle (flash/reboot) while brewing | GPIO defaults LOW → heater/pump/valves all off | ✅ Hardware default |
| Power cycle while steam purge is running | Purge stops (GPIO reset); machine boots off | ✅ Hardware default |

---

## Detailed Scenario Descriptions

### ✅ Power OFF During Active Steaming

**Scenario:** The user presses the power switch OFF while the steam wand is
actively steaming (steam valve open, pump running, thermoblock at ~135 °C).

**Why it is dangerous without protection:**
Hot, pressurised water/steam is trapped in the circuit. Abruptly closing the
steam valve without releasing pressure can cause a pressure spike that stresses
fittings. Leaving the heater at 135 °C unattended is a burn/fire risk.

**What the firmware does:**

1. `machine_off()` detects `SteamState::STEAMING` and calls `steam_stop()`.
2. `steam_stop()` immediately:
   - Closes the steam valve
   - Stops the pump
   - **Opens the purge valve** (releases residual pressure safely)
   - Lowers the heater setpoint to `cool_down_to` (default 90 °C)
   - Transitions to `SteamState::COOLING`
3. The orchestrator loop continues advancing COOLING → CLEANUP → IDLE:
   - COOLING: purge valve stays open; waits for temperature to drop to `cool_down_to`
   - CLEANUP: purge valve closes; transitions to IDLE
4. The YAML `turn_off_action` also sets the PID climate to `mode: off`,
   stopping the heater element entirely.

**Net result:** Steam pressure is safely vented through the purge path. The
thermoblock cools naturally. The machine reaches a safe idle state without
trapping pressure or leaving the heater energised.

---

### ✅ Quick Power OFF → ON After Steaming

**Scenario:** The user steams milk, turns the machine OFF, and then immediately
presses ON again before the purge sequence finishes.

**Why it matters:**
If the purge sequence were interrupted by the ON command, hot steam pressure
could remain trapped, and a new brew might start before the circuit has cooled.

**What the firmware does:**

1. `machine_off()` calls `steam_stop()` → COOLING begins (purge valve open).
2. `machine_on()` sets `powered_on_ = true` immediately, but `mode_` is still
   `EspressoMode::STEAMING` (in COOLING sub-state).
3. `brew_start()` and `steam_start()` check `mode_ != EspressoMode::IDLE` **in
   addition to** `powered_on_`.  Since mode is still STEAMING, both are rejected.
4. The purge sequence completes autonomously: COOLING → CLEANUP → IDLE.
5. Once `mode_` becomes IDLE, new brew or steam operations are accepted.

**Net result:** The user can flip the power ON immediately — the heater starts
warming back to brew temperature — but no new shot or steam cycle can start
until the purge finishes and the circuit is safe.

---

### ✅ Power OFF During Steam Heat-Up (before steaming begins)

**Scenario:** The user starts the steam sequence (machine heating to 135 °C)
but changes their mind and turns the machine OFF before steaming begins.

**What the firmware does:**

1. `machine_off()` detects `SteamState::HEATING` and calls `steam_stop()`.
2. `steam_stop()` handles the HEATING case: lowers heater setpoint to
   `cool_down_to` and returns to IDLE immediately (no purge needed because
   steam pressure was never built up).
3. `powered_on_` is cleared.

**Net result:** Immediate safe stop. No purge needed because the steam valve
was never opened and no pressure was built.

---

### ✅ Power OFF During Active Brew

**Scenario:** The user presses the power switch OFF while a brew shot is in
progress (pump running, brew valve open).

**Why safe to stop immediately:**
The brew circuit operates at moderate water pressure (~9 bar) but the pump
motor stops immediately when power is removed. Unlike steam, there is no
latent heat risk that requires a cool-down sequence.

**What the firmware does:**

1. `machine_off()` detects `EspressoMode::BREWING`.
2. Calls `safe_stop_all_()`: pump off, all valves closed.
3. Sets `brew_state_ = IDLE` and `mode_ = IDLE` immediately.
4. `powered_on_` is cleared.

**Net result:** Immediate safe stop. The shot is aborted, which wastes coffee
but does not create a safety hazard.

---

### ✅ Machine Starts OFF After Power Failure / Reboot

**Scenario:** The ESP32 loses power mid-brew or mid-steam, then reboots.

**Hardware layer (always active):**
- All GPIO outputs default to LOW on reset.
- The SSR (heater), pump relay, and valve relays all de-energise (safe state).

**Firmware layer:**
- `powered_on_` defaults to `false` in C++ — the machine boots in the OFF state.
- The YAML switch uses `restore_mode: RESTORE_DEFAULT_OFF`, which:
  - On **first boot** (no saved state): switch is OFF.
  - On **subsequent boots**: restores the last saved on/off state from flash.
- If the last saved state was ON, the `turn_on_action` re-enables the heater
  via `climate.control: mode: heat` and calls `machine_on()`.

**Net result:** Hardware is always safe immediately after reset. If the machine
was previously ON, it will resume heating on the next boot after HA reconnects
and restores the switch state — but it will not start a brew or steam
automatically.

---

## Scenarios Not Yet Covered

| Scenario | Gap | Plan | Priority |
|---|---|---|---|
| HA/Wi-Fi disconnect during active brew | Brew continues until `flow_max`; no watchdog timeout | Add timeout config | 🔴 P0 |
| HA/Wi-Fi disconnect during steam cool-down | Purge continues autonomously (correct) | ✅ Handled by state machine | ✅ Done |
| Thermocouple fault → PID drives 100% duty | Hard cutoff at 165 °C fires via `on_value_range` | Add C++ orchestrator test | 🔴 P0 |
| Power ON with thermoblock already at steam temp | Brew actions allowed; caution expected from user | Future UX consideration | 🟡 P2 |
| Power OFF during pre-infusion hold | Pre-infusion is part of BREWING mode — stops immediately | ✅ Covered by "OFF during brew" | ✅ Done |
| Grinder activated during brew/steam | ✅ Allowed by design — operations are independent | No action needed | ✅ N/A |

---

## C++ Test Coverage Reference

The scenarios above are validated by the following GoogleTest tests in
`tests/cpp/test_orchestrator.cpp`:

| Test Name | Scenario Validated |
|---|---|
| `Power.MachineStartsOffByDefault` | Machine boots in off state |
| `Power.MachineOnSetsFlag` | `machine_on()` sets flag |
| `Power.MachineOffClearsFlag` | `machine_off()` clears flag |
| `Power.BrewStartIgnoredWhenOff` | brew_start rejected when off |
| `Power.SteamStartIgnoredWhenOff` | steam_start rejected when off |
| `Power.BrewStartAllowedAfterMachineOn` | brew_start works after on |
| `Power.SteamStartAllowedAfterMachineOn` | steam_start works after on |
| `Power.MachineOffStopsActiveBrew` | OFF during brew → immediate stop |
| `Power.MachineOffDuringSteamingInitiatesPurge` | OFF during steaming → purge starts |
| `Power.PurgeCompletesAfterMachineOffDuringSteaming` | Purge runs to completion |
| `Power.MachineOffDuringHeatUpCancelsImmediately` | OFF during heat-up → immediate cancel |
| `Power.MachineCanBeReusedAfterOffOnCycle` | Machine usable after off/on cycle |
| `Power.MachineOnWhilePurgingAllowsNewOpsAfterIdle` | Quick off/on: new ops wait for purge |
