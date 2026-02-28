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
| Power OFF during brew | Brew stops immediately; shot stats recorded; pump off, valves closed | ✅ Covered (C++ test) |
| Power OFF during steam heat-up | Heat-up cancelled; heater lowered; powered off immediately | ✅ Covered (C++ test) |
| Power OFF during steam purge (PURGING) | Purge cancelled immediately; heater lowered; powered off immediately | ✅ Covered (C++ test) |
| Power OFF during active steaming | Purge sequence initiated; power-off deferred until cooldown completes | ✅ Covered (C++ test) |
| Power OFF during steam cool-down | Power-off deferred; cool-down + purge continues until IDLE | ✅ Covered (C++ test) |
| Power ON mid-purge (quick toggle after steam) | Pending off cancelled; new operations available after IDLE | ✅ Covered (C++ test) |
| Brew/steam start while machine is OFF | Ignored with warning log | ✅ Covered (C++ test) |
| Power cycle (flash/reboot) while brewing | GPIO defaults LOW → heater/pump/valves all off | ✅ Hardware default |
| Power cycle while steam purge is running | Purge stops (GPIO reset); machine boots off | ✅ Hardware default |
| Idle auto-off with HA switch still ON | `power_switch` syncs HA switch to OFF | ✅ Covered (C++ test) |
| Manual brew stop (brew_stop) | Shot stats recorded; heater setpoint restored | ✅ Covered (C++ test) |
| Brew stop during temperature surfing | Heater setpoint restored to `brew_target_temp_` | ✅ Covered (C++ test) |

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
3. `machine_off()` sets `pending_power_off_ = true` instead of immediately
   clearing `powered_on_`.  This ensures the HA power switch stays in sync
   during the cooldown — the machine reports "powered on" while actively
   performing safety-critical work.
4. The orchestrator loop continues advancing COOLING → CLEANUP → IDLE:
   - COOLING: purge valve stays open; waits for temperature to drop to `cool_down_to`
   - CLEANUP: purge valve closes; transitions to IDLE
5. On the CLEANUP → IDLE transition, `pending_power_off_` is detected:
   `powered_on_` is set to false, and the optional `power_switch_` entity
   publishes OFF to HA, keeping the dashboard in sync.
6. The YAML `turn_off_action` also sets the PID climate to `mode: off`,
   stopping the heater element entirely.

> **Note on PURGING state:** If the machine is off-ed during the `PURGING` phase
> (pump flushing water through purge valve before the steam valve opened),
> `machine_off()` calls `steam_stop()` which cancels PURGING immediately:
> pump stops, purge valve closes, heater setpoint is lowered — no cool-down
> is needed because the steam valve was never opened.

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
   `pending_power_off_` is set to `true`; `powered_on_` remains `true`.
2. `machine_on()` clears `pending_power_off_` immediately (even though
   `powered_on_` is already true — the early return still clears the flag).
3. `brew_start()` and `steam_start()` check `mode_ != EspressoMode::IDLE` **in
   addition to** `powered_on_`.  Since mode is still STEAMING, both are rejected.
4. The purge sequence completes autonomously: COOLING → CLEANUP → IDLE.
   Since `pending_power_off_` was cleared by `machine_on()`, `powered_on_`
   remains `true` — the machine is fully usable immediately.
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
2. Records partial shot statistics (time, volume, yield) and publishes them
   to HA sensor entities (if wired) — the shot data is preserved even though
   the shot was interrupted.
3. Restores the heater setpoint to `brew_target_temp_` (in case temperature
   surfing had modified it mid-shot).
4. Calls `safe_stop_all_()`: pump off, all valves closed.
5. Sets `brew_state_ = IDLE` and `mode_ = IDLE` immediately.
6. `powered_on_` is cleared.

**Net result:** Immediate safe stop. The shot is aborted, which wastes coffee
but does not create a safety hazard.  Shot statistics are preserved for
review in Home Assistant.

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
| HA/Wi-Fi disconnect during active brew | Brew continues until `flow_max`; no watchdog timeout | ✅ `set_brew_timeout_ms()` added; default 0 (disabled) | ✅ Done (P0-4) |
| HA/Wi-Fi disconnect during steam cool-down | Purge continues autonomously (correct) | ✅ Handled by state machine | ✅ Done |
| Thermocouple fault → PID drives 100% duty | Hard cutoff at 165 °C fires via `on_value_range` | ✅ `check_over_temp_safety_()` in C++ orchestrator; NaN detection added | ✅ Done (P0-1 / P0-3) |
| Power ON with thermoblock already at steam temp | Brew actions allowed; caution expected from user | Documented below (P2-6) | ✅ Documented |
| Power OFF during pre-infusion hold | Pre-infusion is part of BREWING mode — stops immediately | ✅ Covered by "OFF during brew" | ✅ Done |
| Grinder activated during brew/steam | ✅ Allowed by design — operations are independent | No action needed | ✅ N/A |
| PID-stabilised temperature just below setpoint (e.g. 89.9°C at 90.0°C target) | Machine waits indefinitely in HEATING | `is_ready()` on `IHeater` with configurable `temperature_tolerance` (default 0.5°C) | ✅ Done |

---

### Power ON with Thermoblock Already at Steam Temperature (P2-6)

**Scenario:** The user turns on the espresso machine shortly after a steaming session, while the
thermoblock is still hot (e.g., 130–140 °C). The thermoblock has not yet cooled back to the
brew target temperature of ~90 °C.

**Why it matters:**
If the user immediately presses "brew start", the machine will enter the HEATING state. Without
a `heater_controller:` wired, the machine transitions immediately from HEATING to BREWING without
verifying temperature. With a `heater_controller:` wired (recommended), the machine waits in
HEATING until the temperature drops to `target_temperature` — which means it actually waits for
the thermoblock to **cool down**, not heat up.

**What the firmware does:**

1. `brew_start()` is accepted (machine is IDLE and powered on — no interlock on temperature at start).
2. If `heater_controller:` is wired, the HEATING state gates the HEATING→BREWING transition on
   `heater.is_ready(brew_target_temp_)`. If the thermoblock is above brew temperature,
   the state machine waits in HEATING until the block cools to the brew setpoint.
3. If no `heater_controller:` is wired, the machine transitions to BREWING immediately. The resulting
   shot will be extracted at higher temperature, which may over-extract or cause bitter flavour.
   This is a **user experience concern, not a safety concern** — the machine operates correctly
   and within hardware limits at any temperature below the over-temp cutoff (165 °C).

**Recommended practice:** Always wire `heater_controller:` in the brew section of your YAML.
This gives you proper temperature-gated transitions in both directions (too cold: waits to heat up;
too hot: waits to cool down).

**No firmware interlock added:** Adding a hard interlock that refuses brew when too hot would
prevent legitimate use cases (e.g., pulling a second shot at a slightly elevated temperature
when using temperature surfing). The user is responsible for timing their shots appropriately.



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
| `SteamPurge.MachineOffDuringPurgingCancelsImmediately` | OFF during PURGING → immediate cancel |
| `SteamPurge.FullSteamSequenceWithPurge` | Full HEATING→PURGING→STEAMING→COOLING→IDLE sequence |
| `SteamTimeout.SteamTimesOutWhenTimeoutElapses` | Steam auto-stops after configured timeout |
| `HeaterReadiness.BrewProceedsWhenTempStabilisedJustBelowTarget` | 89.9°C at 90.0°C target with 0.5°C tolerance → brew starts |
| `HeaterReadiness.BrewWaitsWhenTempBelowToleranceBand` | 89.4°C at 90.0°C target with 0.5°C tolerance → brew waits |
| `HeaterReadiness.DefaultIsReadyRequiresExactTarget` | Default IHeater (no tolerance) requires exact >= target |
| `HeaterReadiness.SteamProceedsWhenTempStabilisedJustBelowSteamTarget` | 134.7°C at 135.0°C steam target with 0.5°C tolerance → steam starts |
| `HeaterReadiness.SteamWaitsWhenTempBelowSteamToleranceBand` | 134.0°C at 135.0°C steam target with 0.5°C tolerance → steam waits |
| `BrewTemperatureCooldown.DisabledByDefaultNoCoolingStateAtEnd` | DONE→CLEANUP without COOLING when disabled |
| `BrewTemperatureCooldown.DisabledSkipsCoolingEvenWhenTempAboveTarget` | COOLING skipped when feature disabled |
| `BrewTemperatureCooldown.EnabledButNoHeaterCtrlSkipsCooling` | COOLING skipped when no heater controller wired |
| `BrewTemperatureCooldown.EnabledTempAtTargetGoesDirectlyToHeating` | brew_start() at target temp → direct HEATING |
| `BrewTemperatureCooldown.EnabledTempBelowTargetGoesDirectlyToHeating` | brew_start() below target temp → direct HEATING |
| `BrewTemperatureCooldown.EntersCoolingWhenTempAboveTarget` | brew_start() above target → COOLING first |
| `BrewTemperatureCooldown.CoolingOpensPurgeValveAndRunsPump` | Purge valve + pump active during brew COOLING |
| `BrewTemperatureCooldown.CoolingKeepsBrewValveClosed` | Brew valve stays closed during COOLING |
| `BrewTemperatureCooldown.CoolingSetsHeaterSetpointToBrewTarget` | Heater setpoint lowered to brew target on COOLING entry |
| `BrewTemperatureCooldown.StaysInCoolingWhileTempAboveTarget` | COOLING persists while temp > target |
| `BrewTemperatureCooldown.TransitionsToHeatingWhenTempReachesTarget` | COOLING→HEATING when temp ≤ target |
| `BrewTemperatureCooldown.HeatingAfterCoolingClosesPurgeValveAndStopsPump` | Pump off + purge valve closed at HEATING entry |
| `BrewTemperatureCooldown.FullSequenceWithCooldownReturnsToIdle` | Full COOLING→HEATING→BREWING→DONE→CLEANUP→IDLE |
| `BrewTemperatureCooldown.BrewStopDuringCoolingSafelyReturnsToIdle` | brew_stop() during COOLING → immediate safe stop |
| `BrewTemperatureCooldown.StatusNameShowsCurrentAndTargetTemperatures` | Status string shows current and target °C during COOLING |
| `BrewTemperatureCooldown.NoCoolingInsertedAfterDoneWhenEnabled` | DONE→CLEANUP is direct even with cooldown enabled (no end-of-brew cooling) |
| **Bug-fix tests (power desync, shot stats, heater setpoint)** | |
| `BugFix.DeferredPowerOffDuringSteamCooldown` | powered_on_ stays true during steam cooldown, goes false after IDLE |
| `BugFix.MachineOnCancelsPendingPowerOff` | machine_on() mid-cooldown cancels deferred off; machine stays on |
| `BugFix.MachineOffDuringHeatingPowersOffImmediately` | HEATING cancel needs no cooldown → immediate off |
| `BugFix.MachineOffDuringCoolingDefersPowerOff` | machine_off() during COOLING defers; off after IDLE |
| `BugFix.BrewStopRecordsPartialShotStats` | brew_stop() records time, volume, yield |
| `BugFix.BrewStopRecordsStatsToSensors` | brew_stop() publishes to HA sensor entities |
| `BugFix.BrewStopDuringHeatingDoesNotRecordStats` | brew_stop() before BREWING does not record empty stats |
| `BugFix.BrewStopRestoresHeaterSetpointAfterTempSurfing` | brew_stop() after temp surfing restores setpoint |
| `BugFix.MachineOffDuringBrewRecordsPartialShotStats` | machine_off() during brew records partial stats |
| `BugFix.MachineOffDuringBrewRestoresHeaterSetpoint` | machine_off() during brew restores heater setpoint |
| `BugFix.PowerSwitchSyncedOnIdleAutoOff` | Idle auto-off publishes OFF to HA power switch |
| `BugFix.PowerSwitchSyncedAfterSteamCooldown` | Deferred off publishes OFF to HA power switch after cooldown |
| `BugFix.PowerSwitchNotSyncedWhenMachineOnCancelsPending` | Cancelled pending off does not publish OFF |
