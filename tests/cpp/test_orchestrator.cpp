#include <gtest/gtest.h>
#include <limits>
#include "esphome/core/hal.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "espresso_machine/espresso_machine.h"
#include "espresso_machine/interfaces.h"

using namespace esphome::espresso_machine;

extern uint32_t g_mock_millis;

// ---------------------------------------------------------------------------
// Minimal in-test doubles for the hardware interfaces
// ---------------------------------------------------------------------------
struct MockValve : public IValve {
  int open_count = 0;
  int close_count = 0;
  bool open_state = false;

  void open() override {
    open_count++;
    open_state = true;
  }
  void close() override {
    close_count++;
    open_state = false;
  }
  bool is_open() const override { return open_state; }
};

struct MockPump : public IPump {
  bool running = false;
  int on_count = 0;
  int off_count = 0;
  float volume = 0.0f;   // simulated flow total (ml)
  float rate = 0.0f;     // simulated flow rate (ml/s)
  int reset_flow_count = 0;
  float target_flow = 0.0f;  // last value passed to set_target_flow()

  void turn_on() override {
    running = true;
    on_count++;
  }
  void turn_off() override {
    running = false;
    off_count++;
  }
  bool is_running() const override { return running; }
  float get_flow_rate() const override { return rate; }
  float get_flow_total() const override { return volume; }
  void reset_flow() override {
    volume = 0.0f;
    rate = 0.0f;
    reset_flow_count++;
  }
  void set_target_flow(float ml_per_s) override { target_flow = ml_per_s; }
};

struct OrchestratorFixture {
  MockValve brew_valve;
  MockValve purge_valve;
  MockValve steam_valve;
  MockValve steam_purge_valve;
  MockPump brew_pump;
  MockPump steam_pump;
  EspressoMachine machine;

  OrchestratorFixture() {
    machine.set_brew_valve(&brew_valve);
    machine.set_brew_purge_valve(&purge_valve);
    machine.set_brew_pump(&brew_pump);
    machine.set_brew_target_temperature(90.0f);
    machine.set_brew_flow_max(40.0f);
    machine.set_brew_flow_offset(20.0f);

    machine.set_steam_valve(&steam_valve);
    machine.set_steam_purge_valve(&steam_purge_valve);
    machine.set_steam_pump(&steam_pump);
    machine.set_steam_target_temperature(135.0f);
    machine.set_steam_flow_max(2.0f);
    machine.set_steam_cool_down_to(90.0f);

    g_mock_millis = 0;
    machine.setup();
    machine.machine_on();  // power on so brew/steam actions are accepted
  }
};

// ---------------------------------------------------------------------------
// Initial state
// ---------------------------------------------------------------------------
TEST(Orchestrator, InitialModeIsIdle) {
  OrchestratorFixture f;
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
}

TEST(Orchestrator, SetupClosesAllValvesAndStopsPumps) {
  OrchestratorFixture f;
  EXPECT_FALSE(f.brew_valve.open_state);
  EXPECT_FALSE(f.purge_valve.open_state);
  EXPECT_FALSE(f.steam_valve.open_state);
  EXPECT_FALSE(f.brew_pump.running);
  EXPECT_FALSE(f.steam_pump.running);
}

TEST(Orchestrator, ModeNameIdle) {
  OrchestratorFixture f;
  EXPECT_STREQ(f.machine.mode_name(), "idle");
}

// ---------------------------------------------------------------------------
// Brew start / stop
// ---------------------------------------------------------------------------
TEST(Orchestrator, BrewStartEntersBrewingMode) {
  OrchestratorFixture f;
  f.machine.brew_start();
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::BREWING);
  EXPECT_STREQ(f.machine.mode_name(), "brewing");
}

TEST(Orchestrator, BrewStartResetsFlowMeter) {
  OrchestratorFixture f;
  f.brew_pump.volume = 15.0f;
  f.machine.brew_start();
  EXPECT_EQ(f.brew_pump.reset_flow_count, 1);
}

TEST(Orchestrator, BrewStartKeepsValvesClosedUntilHeating) {
  OrchestratorFixture f;
  f.machine.brew_start();
  // In HEATING state valves remain closed; pump off
  EXPECT_FALSE(f.brew_valve.open_state);
  EXPECT_FALSE(f.brew_pump.running);
}

TEST(Orchestrator, BrewStopReturnsToIdle) {
  OrchestratorFixture f;
  f.machine.brew_start();
  f.machine.brew_stop();
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
}

TEST(Orchestrator, BrewStopTurnsOffPumpAndClosesValves) {
  OrchestratorFixture f;
  f.machine.brew_start();
  // Advance past HEATING so pump and valve are opened
  f.machine.loop();  // HEATING → BREWING (pump on, valve open)
  EXPECT_TRUE(f.brew_pump.running);
  EXPECT_TRUE(f.brew_valve.open_state);

  f.machine.brew_stop();
  EXPECT_FALSE(f.brew_pump.running);
  EXPECT_FALSE(f.brew_valve.open_state);
}

TEST(Orchestrator, BrewStopIgnoredWhenIdle) {
  OrchestratorFixture f;
  f.machine.brew_stop();  // should be a no-op without crash
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
}

// ---------------------------------------------------------------------------
// Brew state machine progression
// ---------------------------------------------------------------------------
TEST(Orchestrator, BrewLoopAdvancesFromHeatingToBrewing) {
  OrchestratorFixture f;
  f.machine.brew_start();
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::HEATING);
  f.machine.loop();  // should transition → BREWING
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::BREWING);
  EXPECT_TRUE(f.brew_pump.running);
  EXPECT_TRUE(f.brew_valve.open_state);
}

TEST(Orchestrator, BrewAutoTerminatesAtFlowMax) {
  OrchestratorFixture f;
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::BREWING);

  // Set flow total on the pump (flow comes through the pump interface)
  f.brew_pump.volume = 40.0f;
  f.machine.loop();  // BREWING → DONE
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::DONE);
  EXPECT_FALSE(f.brew_pump.running);
  EXPECT_FALSE(f.brew_valve.open_state);
}

TEST(Orchestrator, BrewDoesNotTerminateBeforeFlowMax) {
  OrchestratorFixture f;
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING

  f.brew_pump.volume = 39.9f;
  f.machine.loop();  // still BREWING
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::BREWING);
  EXPECT_TRUE(f.brew_pump.running);
}

TEST(Orchestrator, BrewDoneTransitionsToCleanupThenIdle) {
  OrchestratorFixture f;
  f.machine.brew_start();
  f.machine.loop();          // HEATING → BREWING
  f.brew_pump.volume = 40.0f;
  f.machine.loop();          // BREWING → DONE
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::DONE);
  f.machine.loop();          // DONE → CLEANUP
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::CLEANUP);
  f.machine.loop();          // CLEANUP → IDLE
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
}

// ---------------------------------------------------------------------------
// Steam start / stop
// ---------------------------------------------------------------------------
TEST(Orchestrator, SteamStartEntersSteamingMode) {
  OrchestratorFixture f;
  f.machine.steam_start();
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::STEAMING);
  EXPECT_STREQ(f.machine.mode_name(), "steaming");
}

TEST(Orchestrator, SteamStartKeepsSteamValveClosedWhileHeating) {
  OrchestratorFixture f;
  f.machine.steam_start();
  EXPECT_FALSE(f.steam_valve.open_state);
  EXPECT_FALSE(f.steam_pump.running);
}

TEST(Orchestrator, SteamStopReturnsToIdle) {
  OrchestratorFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → STEAMING
  f.machine.steam_stop();
  // Phase 8: steam_stop enters COOLING; advance through COOLING → CLEANUP → IDLE
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::COOLING);
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::STEAMING);
  f.machine.loop();  // COOLING → CLEANUP
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::CLEANUP);
  f.machine.loop();  // CLEANUP → IDLE
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
}

TEST(Orchestrator, SteamStopClosesValveAndStopsPump) {
  OrchestratorFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → STEAMING (valve open, pump on)
  EXPECT_TRUE(f.steam_valve.open_state);
  EXPECT_TRUE(f.steam_pump.running);

  f.machine.steam_stop();
  EXPECT_FALSE(f.steam_valve.open_state);
  EXPECT_FALSE(f.steam_pump.running);
}

TEST(Orchestrator, SteamStopIgnoredWhenIdle) {
  OrchestratorFixture f;
  f.machine.steam_stop();
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
}

// ---------------------------------------------------------------------------
// Safety interlocks
// ---------------------------------------------------------------------------
TEST(Orchestrator, CannotStartBrewWhileSteaming) {
  OrchestratorFixture f;
  f.machine.steam_start();
  f.machine.brew_start();  // must be ignored
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::STEAMING);
}

TEST(Orchestrator, CannotStartSteamWhileBrewing) {
  OrchestratorFixture f;
  f.machine.brew_start();
  f.machine.steam_start();  // must be ignored
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::BREWING);
}

TEST(Orchestrator, IsBusyWhenBrewing) {
  OrchestratorFixture f;
  EXPECT_FALSE(f.machine.is_busy());
  f.machine.brew_start();
  EXPECT_TRUE(f.machine.is_busy());
  f.machine.brew_stop();
  EXPECT_FALSE(f.machine.is_busy());
}

TEST(Orchestrator, IsBusyWhenSteaming) {
  OrchestratorFixture f;
  f.machine.steam_start();
  EXPECT_TRUE(f.machine.is_busy());
  f.machine.loop();  // HEATING → STEAMING
  f.machine.steam_stop();
  // Phase 8: machine stays busy during COOLING and CLEANUP
  EXPECT_TRUE(f.machine.is_busy());
  f.machine.loop();  // COOLING → CLEANUP
  EXPECT_TRUE(f.machine.is_busy());
  f.machine.loop();  // CLEANUP → IDLE
  EXPECT_FALSE(f.machine.is_busy());
}

// ---------------------------------------------------------------------------
// Phase 7 — shot stats
// ---------------------------------------------------------------------------
TEST(Orchestrator, ShotStatsRecordedAtDone) {
  OrchestratorFixture f;
  f.machine.brew_start();
  f.machine.loop();           // HEATING → BREWING
  g_mock_millis = 25000;      // simulate 25 s elapsed
  f.brew_pump.volume = 40.0f;
  f.machine.loop();           // BREWING → DONE
  EXPECT_NEAR(f.machine.get_last_shot_time_s(), 25.0f, 0.1f);
  EXPECT_FLOAT_EQ(f.machine.get_last_shot_volume_ml(), 40.0f);
}

// ---------------------------------------------------------------------------
// Phase 7 — temperature surfing
// ---------------------------------------------------------------------------
TEST(Orchestrator, TempSurfingSettingsAccepted) {
  OrchestratorFixture f;
  f.machine.set_brew_temp_offset(5.0f);
  f.machine.set_brew_temp_ramp_time_ms(20000);
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING (no crash with surfing config set)
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::BREWING);
}

TEST(Orchestrator, TempSurfingDisabledViaSwitch) {
  // When temp_surf_enabled_ is set to false the orchestrator must not apply
  // the surfing ramp — even if offset and ramp_time are configured.
  // Use the simple fixture (no heater controller) so the BREWING tick runs
  // but no setpoint call is made regardless of the enabled flag.
  OrchestratorFixture f;
  f.machine.set_brew_temp_offset(5.0f);
  f.machine.set_brew_temp_ramp_time_ms(20000);
  f.machine.set_temp_surf_enabled(false);

  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::BREWING);
  // No heater controller wired — no crash and machine is still brewing.
}

// ---------------------------------------------------------------------------
// Phase 7 — pre-infusion
// ---------------------------------------------------------------------------
TEST(Orchestrator, PreInfusionDisabledSkipsDirectlyToBrewing) {
  OrchestratorFixture f;
  // pre_infusion_enabled_ defaults to false
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING (no PRE_INFUSION when disabled)
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::BREWING);
}

TEST(Orchestrator, PreInfusionEnabledEntersPreInfusionState) {
  OrchestratorFixture f;
  f.machine.set_pre_infusion_enabled(true);
  f.machine.set_pre_infusion_volume_ml(5.0f);
  f.machine.set_pre_infusion_hold_time_ms(500);
  f.machine.brew_start();
  f.machine.loop();  // HEATING → PRE_INFUSION
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::PRE_INFUSION);
  EXPECT_TRUE(f.brew_pump.running);
  EXPECT_TRUE(f.brew_valve.open_state);
}

TEST(Orchestrator, PreInfusionFlowingStopsAtVolumeGoal) {
  OrchestratorFixture f;
  f.machine.set_pre_infusion_enabled(true);
  f.machine.set_pre_infusion_volume_ml(5.0f);
  f.machine.set_pre_infusion_hold_time_ms(500);
  f.machine.brew_start();
  f.machine.loop();  // HEATING → PRE_INFUSION (pump on)
  EXPECT_TRUE(f.brew_pump.running);

  f.brew_pump.volume = 5.0f;
  f.machine.loop();  // flowing → hold (pump off)
  EXPECT_FALSE(f.brew_pump.running);
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::PRE_INFUSION);
}

TEST(Orchestrator, PreInfusionHoldExpiresTransitionsToBrewing) {
  OrchestratorFixture f;
  f.machine.set_pre_infusion_enabled(true);
  f.machine.set_pre_infusion_volume_ml(5.0f);
  f.machine.set_pre_infusion_hold_time_ms(500);
  f.machine.brew_start();
  f.machine.loop();  // HEATING → PRE_INFUSION
  f.brew_pump.volume = 5.0f;
  f.machine.loop();  // flowing → hold phase; record hold_start_ms

  // Advance time past hold duration
  g_mock_millis += 600;
  f.machine.loop();  // hold complete → BREWING
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::BREWING);
  EXPECT_TRUE(f.brew_pump.running);
}

TEST(Orchestrator, PreInfusionResetsFlowBeforeBrewing) {
  OrchestratorFixture f;
  f.machine.set_pre_infusion_enabled(true);
  f.machine.set_pre_infusion_volume_ml(5.0f);
  f.machine.set_pre_infusion_hold_time_ms(0);
  f.machine.brew_start();
  f.machine.loop();  // HEATING → PRE_INFUSION
  f.brew_pump.volume = 5.0f;
  int resets_before = f.brew_pump.reset_flow_count;
  f.machine.loop();  // flowing → hold (volume reached)
  f.machine.loop();  // hold complete → BREWING (resets flow)
  EXPECT_GT(f.brew_pump.reset_flow_count, resets_before);
}

// ---------------------------------------------------------------------------
// Phase 8 — Steam state machine (COOLING → CLEANUP → IDLE)
// ---------------------------------------------------------------------------
TEST(Orchestrator, SteamStopDuringHeatingCancelsImmediately) {
  OrchestratorFixture f;
  f.machine.steam_start();
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::HEATING);
  // Stopping during HEATING goes straight to IDLE (no cool-down needed)
  f.machine.steam_stop();
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::IDLE);
}

TEST(Orchestrator, SteamStopDuringSteamingEntersCooling) {
  OrchestratorFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → STEAMING
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::STEAMING);
  f.machine.steam_stop();
  // Phase 8: cool-down initiated
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::COOLING);
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::STEAMING);
}

TEST(Orchestrator, SteamCoolingTransitionsToCleanup) {
  OrchestratorFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → STEAMING
  f.machine.steam_stop();  // STEAMING → COOLING (purge valve opens here)
  f.machine.loop();  // COOLING → CLEANUP
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::CLEANUP);
  EXPECT_TRUE(f.steam_purge_valve.open_state);
}

TEST(Orchestrator, SteamCleanupTransitionsToIdle) {
  OrchestratorFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → STEAMING
  f.machine.steam_stop();  // STEAMING → COOLING
  f.machine.loop();  // COOLING → CLEANUP
  f.machine.loop();  // CLEANUP → IDLE (purge valve closes)
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
  EXPECT_FALSE(f.steam_purge_valve.open_state);
}

// ---------------------------------------------------------------------------
// Steam pump flow control — delegated to the pump (P2-7)
//
// The orchestrator's responsibility is to tell the pump what flow rate it
// needs; the pump handles bang-bang modulation in its own loop().
// ---------------------------------------------------------------------------

TEST(Orchestrator, SteamSetsTargetFlowOnPumpWhenEnteringSteaming) {
  OrchestratorFixture f;
  // Initially no flow target
  EXPECT_FLOAT_EQ(f.steam_pump.target_flow, 0.0f);

  f.machine.steam_start();
  f.machine.loop();  // HEATING → STEAMING (pump on, target flow set)
  EXPECT_TRUE(f.steam_pump.running);
  // Orchestrator delegates bang-bang to the pump by setting target_flow
  EXPECT_FLOAT_EQ(f.steam_pump.target_flow, 2.0f);  // steam_flow_max configured as 2.0 ml/s
}

TEST(Orchestrator, SteamStopClearsTargetFlowOnPump) {
  OrchestratorFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → STEAMING
  EXPECT_FLOAT_EQ(f.steam_pump.target_flow, 2.0f);

  f.machine.steam_stop();  // STEAMING → COOLING
  // Pump flow control must be disabled before stopping
  EXPECT_FLOAT_EQ(f.steam_pump.target_flow, 0.0f);
  EXPECT_FALSE(f.steam_pump.running);
}

TEST(Orchestrator, SafeStopClearsTargetFlowOnBothPumps) {
  // Verify that an emergency stop (brew_stop or machine_off) also clears
  // the target_flow on both pumps so they don't restart on the next loop().
  OrchestratorFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → STEAMING
  EXPECT_FLOAT_EQ(f.steam_pump.target_flow, 2.0f);

  f.machine.steam_stop();  // exits STEAMING, which clears target_flow
  EXPECT_FLOAT_EQ(f.steam_pump.target_flow, 0.0f);
  EXPECT_FLOAT_EQ(f.brew_pump.target_flow, 0.0f);
}

TEST(Orchestrator, SteamPumpRunsContinuouslyWithoutFlowMeter) {
  OrchestratorFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → STEAMING (pump on, target flow set)
  // When no flow meter is wired the pump's own loop() keeps it running
  // (rate = 0 < target); verify the orchestrator starts the pump and sets target.
  EXPECT_TRUE(f.steam_pump.running);
  EXPECT_FLOAT_EQ(f.steam_pump.target_flow, 2.0f);
}

TEST(Orchestrator, SteamStopIgnoredDuringCooling) {
  OrchestratorFixture f;
  f.machine.steam_start();
  f.machine.loop();   // HEATING → STEAMING
  f.machine.steam_stop();  // STEAMING → COOLING
  // Second steam_stop() during COOLING is a no-op
  f.machine.steam_stop();
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::COOLING);
}

// ---------------------------------------------------------------------------
// Steam temperature management — IHeater-gated transitions
// ---------------------------------------------------------------------------

// Simple in-test double for IHeater
struct MockHeaterCtrl : public IHeater {
  float current_temp{25.0f};
  float target_temp{0.0f};
  int set_target_count{0};

  float get_current_temperature() const override { return current_temp; }
  void set_target_temperature(float t) override {
    target_temp = t;
    set_target_count++;
  }
};

// Fixture with heater controller wired (start cold, target 135°C, cool-down 90°C)
struct OrchestratorWithHeaterFixture {
  MockValve brew_valve;
  MockValve purge_valve;
  MockValve steam_valve;
  MockValve steam_purge_valve;
  MockPump brew_pump;
  MockPump steam_pump;
  MockHeaterCtrl heater_ctrl;
  EspressoMachine machine;

  OrchestratorWithHeaterFixture() {
    machine.set_brew_valve(&brew_valve);
    machine.set_brew_purge_valve(&purge_valve);
    machine.set_brew_pump(&brew_pump);
    machine.set_brew_target_temperature(90.0f);
    machine.set_brew_flow_max(40.0f);
    machine.set_brew_flow_offset(20.0f);

    machine.set_steam_valve(&steam_valve);
    machine.set_steam_purge_valve(&steam_purge_valve);
    machine.set_steam_pump(&steam_pump);
    machine.set_steam_target_temperature(135.0f);
    machine.set_steam_flow_max(2.0f);
    machine.set_steam_cool_down_to(90.0f);
    machine.set_steam_heater_ctrl(&heater_ctrl);

    heater_ctrl.current_temp = 25.0f;  // Start cold

    g_mock_millis = 0;
    machine.setup();
    machine.machine_on();  // power on so brew/steam actions are accepted
  }
};

TEST(Orchestrator, TempSurfingEnabledRaisesSetpoint) {
  // When temp_surf_enabled_ is true and offset > 0, the BREWING tick should
  // apply the surfing ramp.  At t=0 elapsed the desired setpoint is
  // target + offset (5+90=95°C).
  OrchestratorWithHeaterFixture f;
  f.machine.set_brew_temp_offset(5.0f);
  f.machine.set_brew_temp_ramp_time_ms(20000);
  f.machine.set_brew_heater_ctrl(&f.heater_ctrl);
  f.machine.set_brew_target_temperature(90.0f);
  f.machine.set_temp_surf_enabled(true);  // surfing enabled (default)

  f.heater_ctrl.current_temp = 90.0f;
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING (transition; surfing tick not yet run)
  f.machine.loop();  // BREWING tick fires; surfing applies setpoint = 90 + 5 = 95
  // At t≈0 the surfing tick sets desired = 90 + 5*(1 - 0/20000) ≈ 95
  EXPECT_GT(f.heater_ctrl.target_temp, 90.0f);
}

TEST(Orchestrator, TempSurfingDisabledViaFlagDoesNotRaiseSetpoint) {
  // With surfing disabled, the heater target should stay at or below
  // brew_target_temp_ even when offset/ramp_time are configured.
  OrchestratorWithHeaterFixture f;
  f.machine.set_brew_temp_offset(5.0f);
  f.machine.set_brew_temp_ramp_time_ms(20000);
  f.machine.set_brew_heater_ctrl(&f.heater_ctrl);
  f.machine.set_brew_target_temperature(90.0f);
  f.machine.set_temp_surf_enabled(false);  // surfing disabled

  f.heater_ctrl.current_temp = 90.0f;
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING
  f.machine.loop();  // BREWING tick: surfing skipped because flag is false
  EXPECT_LE(f.heater_ctrl.target_temp, 90.0f);
}

TEST(Orchestrator, SteamStartSetsHeaterTargetToSteamTemperature) {
  OrchestratorWithHeaterFixture f;
  f.machine.steam_start();
  EXPECT_FLOAT_EQ(f.heater_ctrl.target_temp, 135.0f);
  EXPECT_EQ(f.heater_ctrl.set_target_count, 1);
}

TEST(Orchestrator, SteamHeatingWaitsForTemperatureWhenHeaterWired) {
  OrchestratorWithHeaterFixture f;
  f.machine.steam_start();
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::HEATING);

  // Simulate temperature still below target — HEATING should not transition
  f.heater_ctrl.current_temp = 100.0f;
  f.machine.loop();
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::HEATING);
  EXPECT_FALSE(f.steam_valve.open_state);
  EXPECT_FALSE(f.steam_pump.running);
}

TEST(Orchestrator, SteamHeatingTransitionsWhenTemperatureReached) {
  OrchestratorWithHeaterFixture f;
  f.machine.steam_start();

  // Simulate temperature reaching target
  f.heater_ctrl.current_temp = 135.0f;
  f.machine.loop();  // HEATING → STEAMING
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::STEAMING);
  EXPECT_TRUE(f.steam_valve.open_state);
  EXPECT_TRUE(f.steam_pump.running);
}

TEST(Orchestrator, SteamStopOpensPurgeValveForCooldown) {
  OrchestratorWithHeaterFixture f;
  f.machine.steam_start();
  f.heater_ctrl.current_temp = 135.0f;
  f.machine.loop();  // HEATING → STEAMING

  f.machine.steam_stop();
  EXPECT_FALSE(f.steam_valve.open_state);
  EXPECT_FALSE(f.steam_pump.running);
  EXPECT_TRUE(f.steam_purge_valve.open_state);  // purge opens immediately on stop
}

TEST(Orchestrator, SteamStopSetsHeaterTargetToCoolDownTemperature) {
  OrchestratorWithHeaterFixture f;
  f.machine.steam_start();
  f.heater_ctrl.current_temp = 135.0f;
  f.machine.loop();  // HEATING → STEAMING

  int count_before = f.heater_ctrl.set_target_count;
  f.machine.steam_stop();
  EXPECT_GT(f.heater_ctrl.set_target_count, count_before);
  EXPECT_FLOAT_EQ(f.heater_ctrl.target_temp, 90.0f);
}

TEST(Orchestrator, SteamCoolingWaitsForTemperatureWhenHeaterWired) {
  OrchestratorWithHeaterFixture f;
  f.machine.steam_start();
  f.heater_ctrl.current_temp = 135.0f;
  f.machine.loop();  // HEATING → STEAMING
  f.machine.steam_stop();  // STEAMING → COOLING
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::COOLING);

  // Temperature still above cool_down_to (90°C) — should stay in COOLING
  f.heater_ctrl.current_temp = 120.0f;
  f.machine.loop();
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::COOLING);
  EXPECT_TRUE(f.steam_purge_valve.open_state);  // purge valve stays open
}

TEST(Orchestrator, SteamCoolingTransitionsWhenTemperatureDropped) {
  OrchestratorWithHeaterFixture f;
  f.machine.steam_start();
  f.heater_ctrl.current_temp = 135.0f;
  f.machine.loop();  // HEATING → STEAMING
  f.machine.steam_stop();  // STEAMING → COOLING

  // Temperature drops to cool_down_to
  f.heater_ctrl.current_temp = 90.0f;
  f.machine.loop();  // COOLING → CLEANUP
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::CLEANUP);
  EXPECT_TRUE(f.steam_purge_valve.open_state);  // still open until CLEANUP runs
  f.machine.loop();  // CLEANUP → IDLE (purge valve closes)
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
  EXPECT_FALSE(f.steam_purge_valve.open_state);
}

TEST(Orchestrator, SteamStopDuringHeatingResetsHeaterSetpoint) {
  OrchestratorWithHeaterFixture f;
  f.machine.steam_start();
  EXPECT_FLOAT_EQ(f.heater_ctrl.target_temp, 135.0f);

  // Cancel before steaming starts
  f.heater_ctrl.current_temp = 80.0f;
  f.machine.steam_stop();
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
  EXPECT_FLOAT_EQ(f.heater_ctrl.target_temp, 90.0f);  // lowered to cool_down_to
}

// ---------------------------------------------------------------------------
// Power on / off
// ---------------------------------------------------------------------------

// Dedicated fixture that does NOT call machine_on() — used to test the off state.
struct OrchestratorOffFixture {
  MockValve brew_valve;
  MockValve purge_valve;
  MockValve steam_valve;
  MockValve steam_purge_valve;
  MockPump brew_pump;
  MockPump steam_pump;
  EspressoMachine machine;

  OrchestratorOffFixture() {
    machine.set_brew_valve(&brew_valve);
    machine.set_brew_purge_valve(&purge_valve);
    machine.set_brew_pump(&brew_pump);
    machine.set_brew_target_temperature(90.0f);
    machine.set_brew_flow_max(40.0f);
    machine.set_brew_flow_offset(20.0f);

    machine.set_steam_valve(&steam_valve);
    machine.set_steam_purge_valve(&steam_purge_valve);
    machine.set_steam_pump(&steam_pump);
    machine.set_steam_target_temperature(135.0f);
    machine.set_steam_flow_max(2.0f);
    machine.set_steam_cool_down_to(90.0f);

    g_mock_millis = 0;
    machine.setup();
    // NOTE: machine_on() intentionally NOT called — machine starts off
  }
};

TEST(Power, MachineStartsOffByDefault) {
  OrchestratorOffFixture f;
  EXPECT_FALSE(f.machine.is_powered_on());
}

TEST(Power, MachineOnSetsFlag) {
  OrchestratorOffFixture f;
  f.machine.machine_on();
  EXPECT_TRUE(f.machine.is_powered_on());
}

TEST(Power, MachineOffClearsFlag) {
  OrchestratorOffFixture f;
  f.machine.machine_on();
  f.machine.machine_off();
  EXPECT_FALSE(f.machine.is_powered_on());
}

TEST(Power, BrewStartIgnoredWhenOff) {
  OrchestratorOffFixture f;
  f.machine.brew_start();
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
}

TEST(Power, SteamStartIgnoredWhenOff) {
  OrchestratorOffFixture f;
  f.machine.steam_start();
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
}

TEST(Power, BrewStartAllowedAfterMachineOn) {
  OrchestratorOffFixture f;
  f.machine.machine_on();
  f.machine.brew_start();
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::BREWING);
}

TEST(Power, SteamStartAllowedAfterMachineOn) {
  OrchestratorOffFixture f;
  f.machine.machine_on();
  f.machine.steam_start();
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::STEAMING);
}

TEST(Power, MachineOffStopsActiveBrew) {
  OrchestratorOffFixture f;
  f.machine.machine_on();
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING (pump on, valve open)
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::BREWING);

  f.machine.machine_off();
  // Must immediately return to IDLE with all hardware off
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
  EXPECT_FALSE(f.brew_pump.running);
  EXPECT_FALSE(f.brew_valve.open_state);
  EXPECT_FALSE(f.machine.is_powered_on());
}

TEST(Power, MachineOffDuringSteamingInitiatesPurge) {
  OrchestratorOffFixture f;
  f.machine.machine_on();
  f.machine.steam_start();
  f.machine.loop();  // HEATING → STEAMING (steam valve open, pump on)
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::STEAMING);

  f.machine.machine_off();
  // steam_stop() was called internally — should now be in COOLING with purge valve open
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::COOLING);
  EXPECT_TRUE(f.steam_purge_valve.open_state);
  EXPECT_FALSE(f.steam_valve.open_state);
  EXPECT_FALSE(f.steam_pump.running);
  // Mode is still STEAMING (completing cooldown) but machine is marked off
  EXPECT_FALSE(f.machine.is_powered_on());
}

TEST(Power, PurgeCompletesAfterMachineOffDuringSteaming) {
  OrchestratorOffFixture f;
  f.machine.machine_on();
  f.machine.steam_start();
  f.machine.loop();     // HEATING → STEAMING
  f.machine.machine_off();   // STEAMING → COOLING (purge valve open)
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::COOLING);
  f.machine.loop();     // COOLING → CLEANUP
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::CLEANUP);
  f.machine.loop();     // CLEANUP → IDLE (purge valve closes)
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
  EXPECT_FALSE(f.steam_purge_valve.open_state);
}

TEST(Power, MachineOffDuringHeatUpCancelsImmediately) {
  OrchestratorOffFixture f;
  f.machine.machine_on();
  f.machine.steam_start();
  // Still in HEATING (no loop tick) → machine_off should cancel immediately
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::HEATING);
  f.machine.machine_off();
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
  EXPECT_FALSE(f.machine.is_powered_on());
}

TEST(Power, MachineCanBeReusedAfterOffOnCycle) {
  OrchestratorOffFixture f;
  f.machine.machine_on();
  f.machine.brew_start();
  f.machine.machine_off();  // stop mid-brew
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);

  // Turn machine back on and verify new brew works
  f.machine.machine_on();
  f.machine.brew_start();
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::BREWING);
}

TEST(Power, MachineOnWhilePurgingAllowsNewOpsAfterIdle) {
  OrchestratorOffFixture f;
  f.machine.machine_on();
  f.machine.steam_start();
  f.machine.loop();          // HEATING → STEAMING
  f.machine.machine_off();   // STEAMING → COOLING (purge starts)

  // Turn back on mid-purge
  f.machine.machine_on();
  EXPECT_TRUE(f.machine.is_powered_on());

  // Must NOT be able to start new operations while purge is still running
  f.machine.brew_start();
  EXPECT_NE(f.machine.get_mode(), EspressoMode::BREWING);  // still in STEAMING (COOLING)

  // Complete the purge
  f.machine.loop();   // COOLING → CLEANUP
  f.machine.loop();   // CLEANUP → IDLE

  // Now a new brew should work
  f.machine.brew_start();
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::BREWING);
}

// ---------------------------------------------------------------------------
// 🔴 P0-1 / P0-3 — Hard over-temperature cutoff and sensor fault handling
// ---------------------------------------------------------------------------

// MockHeaterCtrl that can be configured to return a specific temperature
// (or NaN to simulate a sensor fault).  Also records if force_off() is called.
struct SafetyMockHeaterCtrl : public IHeater {
  float current_temp{25.0f};
  float target_temp{0.0f};
  int force_off_count{0};

  float get_current_temperature() const override { return current_temp; }
  void set_target_temperature(float t) override { target_temp = t; }
  void force_off() override { force_off_count++; }
};

// Fixture with an over-temp sensor wired to the orchestrator.
struct SafetyFixture {
  MockValve brew_valve;
  MockValve purge_valve;
  MockValve steam_valve;
  MockValve steam_purge_valve;
  MockPump brew_pump;
  MockPump steam_pump;
  SafetyMockHeaterCtrl over_temp_sensor;
  EspressoMachine machine;

  SafetyFixture() {
    machine.set_brew_valve(&brew_valve);
    machine.set_brew_purge_valve(&purge_valve);
    machine.set_brew_pump(&brew_pump);
    machine.set_brew_target_temperature(90.0f);
    machine.set_brew_flow_max(40.0f);
    machine.set_brew_flow_offset(20.0f);

    machine.set_steam_valve(&steam_valve);
    machine.set_steam_purge_valve(&steam_purge_valve);
    machine.set_steam_pump(&steam_pump);
    machine.set_steam_target_temperature(135.0f);
    machine.set_steam_flow_max(2.0f);
    machine.set_steam_cool_down_to(90.0f);

    machine.set_over_temp_sensor(&over_temp_sensor);
    machine.set_over_temp_cutoff_limit(165.0f);

    g_mock_millis = 0;
    machine.setup();
    machine.machine_on();
  }
};

// P0-1: Temperature at the limit triggers cutoff within one loop tick.
TEST(Safety, OverTempCutoffFiredWhenSensorExceedsLimit) {
  SafetyFixture f;
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING

  f.over_temp_sensor.current_temp = 166.0f;  // above 165 °C limit
  f.machine.loop();  // cutoff should fire

  EXPECT_TRUE(f.machine.is_over_temp_cutoff_triggered());
}

// P0-1: After cutoff the pump is off and valves are closed.
TEST(Safety, OverTempCutoffStopsAllHardware) {
  SafetyFixture f;
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING (pump on, valve open)
  EXPECT_TRUE(f.brew_pump.running);

  f.over_temp_sensor.current_temp = 166.0f;
  f.machine.loop();  // cutoff fires

  EXPECT_FALSE(f.brew_pump.running);
  EXPECT_FALSE(f.brew_valve.open_state);
}

// P0-1: After cutoff force_off() is called on the heater sensor.
TEST(Safety, OverTempCutoffCallsForceOff) {
  SafetyFixture f;
  f.over_temp_sensor.current_temp = 166.0f;
  f.machine.loop();  // cutoff fires immediately

  EXPECT_GE(f.over_temp_sensor.force_off_count, 1);
}

// P0-1: The cutoff is a latch — once triggered brew/steam cannot restart.
// "Safety_PIDNotResumeAfterCutoff" from mock_scenarios.md.
TEST(Safety, PIDNotResumeAfterCutoff) {
  SafetyFixture f;
  f.over_temp_sensor.current_temp = 166.0f;
  f.machine.loop();  // cutoff fires
  EXPECT_TRUE(f.machine.is_over_temp_cutoff_triggered());

  // Bring temperature back to normal — cutoff must remain latched.
  f.over_temp_sensor.current_temp = 90.0f;
  f.machine.brew_start();
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);  // still blocked

  f.machine.steam_start();
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);  // still blocked
}

// P0-1: Loop returns early after cutoff — no new actions taken on hardware.
TEST(Safety, OverTempCutoffBlocksSubsequentLoopTicks) {
  SafetyFixture f;
  f.over_temp_sensor.current_temp = 166.0f;
  f.machine.loop();  // cutoff fires
  int off_count = f.brew_pump.off_count;

  // Additional loop ticks must not call turn_off() again
  f.machine.loop();
  f.machine.loop();
  EXPECT_EQ(f.brew_pump.off_count, off_count);
}

// P0-1: Cutoff also fires correctly when machine is in IDLE (no active shot).
TEST(Safety, OverTempCutoffFiredDuringIdle) {
  SafetyFixture f;
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
  f.over_temp_sensor.current_temp = 170.0f;
  f.machine.loop();
  EXPECT_TRUE(f.machine.is_over_temp_cutoff_triggered());
}

// P0-3: NaN temperature (sensor fault) triggers the safety cutoff.
TEST(Safety, SensorNaNForcesHeaterOff) {
  SafetyFixture f;
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING

  f.over_temp_sensor.current_temp = std::numeric_limits<float>::quiet_NaN();
  f.machine.loop();  // sensor fault detected

  EXPECT_TRUE(f.machine.is_over_temp_cutoff_triggered());
  EXPECT_GE(f.over_temp_sensor.force_off_count, 1);
  EXPECT_FALSE(f.brew_pump.running);
  EXPECT_FALSE(f.brew_valve.open_state);
}

// P0-3: NaN triggers cutoff even when machine is idle.
TEST(Safety, SensorNaNCutoffFiredDuringIdle) {
  SafetyFixture f;
  f.over_temp_sensor.current_temp = std::numeric_limits<float>::quiet_NaN();
  f.machine.loop();
  EXPECT_TRUE(f.machine.is_over_temp_cutoff_triggered());
}

// No over-temp sensor wired → cutoff never fires (backward-compatible path).
TEST(Safety, NoCutoffWithoutSensorWired) {
  OrchestratorFixture f;  // does not wire an over-temp sensor
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING
  // No sensor wired — cutoff flag stays false regardless.
  EXPECT_FALSE(f.machine.is_over_temp_cutoff_triggered());
}

// ---------------------------------------------------------------------------
// 🔴 P0-4 — Brew timeout (Wi-Fi / HA disconnect safety)
// ---------------------------------------------------------------------------

// Fixture that enables a 30-second brew timeout.
struct BrewTimeoutFixture {
  static constexpr uint32_t kBrewTimeoutMs = 30000;

  MockValve brew_valve;
  MockValve purge_valve;
  MockValve steam_valve;
  MockValve steam_purge_valve;
  MockPump brew_pump;
  MockPump steam_pump;
  EspressoMachine machine;

  BrewTimeoutFixture() {
    machine.set_brew_valve(&brew_valve);
    machine.set_brew_purge_valve(&purge_valve);
    machine.set_brew_pump(&brew_pump);
    machine.set_brew_target_temperature(90.0f);
    machine.set_brew_flow_max(40.0f);
    machine.set_brew_flow_offset(20.0f);
    machine.set_brew_timeout_ms(kBrewTimeoutMs);

    machine.set_steam_valve(&steam_valve);
    machine.set_steam_purge_valve(&steam_purge_valve);
    machine.set_steam_pump(&steam_pump);
    machine.set_steam_target_temperature(135.0f);
    machine.set_steam_flow_max(2.0f);
    machine.set_steam_cool_down_to(90.0f);

    g_mock_millis = 0;
    machine.setup();
    machine.machine_on();
  }
};

// P0-4: Brew stops when timeout expires even if flow_max is never reached.
// Models the scenario where the flow sensor is stuck at 0 after a Wi-Fi
// disconnect prevents a manual stop from Home Assistant.
TEST(Safety, BrewTimesOutWhenFlowNeverReachesMax) {
  BrewTimeoutFixture f;
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING

  // Flow sensor stuck at 0 — flow_max would never be reached
  EXPECT_EQ(f.brew_pump.volume, 0.0f);

  // Advance time past the 30-second timeout
  g_mock_millis = BrewTimeoutFixture::kBrewTimeoutMs + 1000;
  f.machine.loop();  // timeout fires

  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
  EXPECT_FALSE(f.brew_pump.running);
  EXPECT_FALSE(f.brew_valve.open_state);
}

// P0-4: Brew does NOT time out before the timeout has elapsed.
TEST(Safety, BrewDoesNotTimeOutBeforeTimeout) {
  BrewTimeoutFixture f;
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING

  g_mock_millis = BrewTimeoutFixture::kBrewTimeoutMs - 1000;  // just under timeout
  f.machine.loop();
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::BREWING);
}

// P0-4: Normal shot that reaches flow_max before timeout is unaffected.
TEST(Safety, BrewCompletesNormallyBeforeTimeout) {
  BrewTimeoutFixture f;
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING

  // Shot completes normally at 20 s (well before 30 s timeout)
  g_mock_millis = 20000;
  f.brew_pump.volume = 40.0f;
  f.machine.loop();  // BREWING → DONE (flow_max reached)
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::DONE);
}

// P0-4: Timeout = 0 (disabled by default) — brew runs indefinitely.
TEST(Safety, BrewTimeoutDisabledByDefault) {
  OrchestratorFixture f;  // no timeout configured (default 0)
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING

  // Advance to an absurdly long time — brew must still be active
  g_mock_millis = 600000;  // 10 minutes
  f.machine.loop();
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::BREWING);
}

// ---------------------------------------------------------------------------
// 🟠 P1-2 — Brew heater controller: setpoint wiring and HEATING gate
// ---------------------------------------------------------------------------

// Fixture with a brew heater controller wired (cold start, brew target 90°C).
struct BrewHeaterFixture {
  MockValve brew_valve;
  MockValve purge_valve;
  MockValve steam_valve;
  MockValve steam_purge_valve;
  MockPump brew_pump;
  MockPump steam_pump;
  MockHeaterCtrl brew_heater_ctrl;
  EspressoMachine machine;

  BrewHeaterFixture() {
    machine.set_brew_valve(&brew_valve);
    machine.set_brew_purge_valve(&purge_valve);
    machine.set_brew_pump(&brew_pump);
    machine.set_brew_target_temperature(90.0f);
    machine.set_brew_flow_max(40.0f);
    machine.set_brew_flow_offset(20.0f);
    machine.set_brew_heater_ctrl(&brew_heater_ctrl);

    machine.set_steam_valve(&steam_valve);
    machine.set_steam_purge_valve(&steam_purge_valve);
    machine.set_steam_pump(&steam_pump);
    machine.set_steam_target_temperature(135.0f);
    machine.set_steam_flow_max(2.0f);
    machine.set_steam_cool_down_to(90.0f);

    brew_heater_ctrl.current_temp = 25.0f;  // Start cold

    g_mock_millis = 0;
    machine.setup();
    machine.machine_on();
  }
};

// P1-2: brew_start() calls set_target_temperature() on the heater controller.
TEST(P1BrewHeater, BrewStartSetsHeaterTargetToBrewTemperature) {
  BrewHeaterFixture f;
  f.machine.brew_start();
  EXPECT_FLOAT_EQ(f.brew_heater_ctrl.target_temp, 90.0f);
  EXPECT_EQ(f.brew_heater_ctrl.set_target_count, 1);
}

// P1-2: HEATING state waits for temperature when heater controller is wired.
TEST(P1BrewHeater, BrewHeatingWaitsForTemperatureWhenHeaterWired) {
  BrewHeaterFixture f;
  f.machine.brew_start();
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::HEATING);

  // Temperature still below target — HEATING should not transition.
  f.brew_heater_ctrl.current_temp = 80.0f;
  f.machine.loop();
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::HEATING);
  EXPECT_FALSE(f.brew_pump.running);
  EXPECT_FALSE(f.brew_valve.open_state);
}

// P1-2: HEATING transitions to BREWING once temperature is reached.
TEST(P1BrewHeater, BrewHeatingTransitionsWhenTemperatureReached) {
  BrewHeaterFixture f;
  f.machine.brew_start();

  f.brew_heater_ctrl.current_temp = 90.0f;
  f.machine.loop();  // HEATING → BREWING
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::BREWING);
  EXPECT_TRUE(f.brew_pump.running);
  EXPECT_TRUE(f.brew_valve.open_state);
}

// P1-2: Without a heater controller the HEATING transition is still immediate.
TEST(P1BrewHeater, BrewHeatingImmediateWithoutHeaterController) {
  OrchestratorFixture f;  // no brew heater ctrl wired
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING immediately
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::BREWING);
}

// ---------------------------------------------------------------------------
// 🟠 Heater readiness tolerance — IHeater::is_ready() (issue: stable temp)
//
// A thermoblock controlled by a PID may stabilise slightly below the setpoint
// due to integral windup or steady-state error (e.g. 89.9°C when target is
// 90.0°C).  The orchestrator must delegate the "ready" decision to the heater
// via IHeater::is_ready() so that a configurable tolerance prevents an
// indefinite wait in the HEATING state.
// ---------------------------------------------------------------------------

// Heater mock that implements is_ready() with a configurable tolerance,
// mirroring EspressoMachineHeater::is_ready() used in production.
struct MockHeaterWithTolerance : public IHeater {
  float current_temp{25.0f};
  float tolerance{0.5f};  // default matches EspressoMachineHeater default

  float get_current_temperature() const override { return current_temp; }
  void set_target_temperature(float /*t*/) override {}  // target flows through is_ready() param
  bool is_ready(float t) const override { return current_temp >= (t - tolerance); }
};

// Fixture using MockHeaterWithTolerance for brew (tolerance = 0.5°C).
struct BrewHeaterToleranceFixture {
  MockValve brew_valve;
  MockValve purge_valve;
  MockValve steam_valve;
  MockValve steam_purge_valve;
  MockPump brew_pump;
  MockPump steam_pump;
  MockHeaterWithTolerance brew_heater_ctrl;
  EspressoMachine machine;

  BrewHeaterToleranceFixture() {
    machine.set_brew_valve(&brew_valve);
    machine.set_brew_purge_valve(&purge_valve);
    machine.set_brew_pump(&brew_pump);
    machine.set_brew_target_temperature(90.0f);
    machine.set_brew_flow_max(40.0f);
    machine.set_brew_flow_offset(20.0f);
    machine.set_brew_heater_ctrl(&brew_heater_ctrl);

    machine.set_steam_valve(&steam_valve);
    machine.set_steam_purge_valve(&steam_purge_valve);
    machine.set_steam_pump(&steam_pump);
    machine.set_steam_target_temperature(135.0f);
    machine.set_steam_flow_max(2.0f);
    machine.set_steam_cool_down_to(90.0f);

    g_mock_millis = 0;
    machine.setup();
    machine.machine_on();
  }
};

// Core bug scenario: thermoblock stabilises at 89.9°C with target 90.0°C.
// Without tolerance the machine would wait indefinitely.  With a 0.5°C
// tolerance the heater reports ready and brewing begins.
TEST(HeaterReadiness, BrewProceedsWhenTempStabilisedJustBelowTarget) {
  BrewHeaterToleranceFixture f;
  f.brew_heater_ctrl.tolerance = 0.5f;

  f.machine.brew_start();
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::HEATING);

  // Thermoblock stabilised at 89.9°C — within 0.5°C tolerance of 90.0°C.
  f.brew_heater_ctrl.current_temp = 89.9f;
  f.machine.loop();  // HEATING → BREWING (is_ready returns true)
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::BREWING);
  EXPECT_TRUE(f.brew_pump.running);
  EXPECT_TRUE(f.brew_valve.open_state);
}

// When temperature is outside the tolerance band the machine keeps waiting.
TEST(HeaterReadiness, BrewWaitsWhenTempBelowToleranceBand) {
  BrewHeaterToleranceFixture f;
  f.brew_heater_ctrl.tolerance = 0.5f;

  f.machine.brew_start();
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::HEATING);

  // 89.4°C is 0.6°C below target — outside the 0.5°C tolerance.
  f.brew_heater_ctrl.current_temp = 89.4f;
  f.machine.loop();
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::HEATING);
  EXPECT_FALSE(f.brew_pump.running);
  EXPECT_FALSE(f.brew_valve.open_state);
}

// Default IHeater::is_ready() (no override) still requires exact >= target.
TEST(HeaterReadiness, DefaultIsReadyRequiresExactTarget) {
  BrewHeaterFixture f;  // uses MockHeaterCtrl — no is_ready() override
  f.machine.brew_start();

  // 89.9°C without a tolerance override does NOT satisfy default >= 90.0°C.
  f.brew_heater_ctrl.current_temp = 89.9f;
  f.machine.loop();
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::HEATING);

  // Reaching exactly the target satisfies the default check.
  f.brew_heater_ctrl.current_temp = 90.0f;
  f.machine.loop();
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::BREWING);
}

// Steam heating: same tolerance logic applies via is_ready().
TEST(HeaterReadiness, SteamProceedsWhenTempStabilisedJustBelowSteamTarget) {
  MockValve brew_valve, purge_valve, steam_valve, steam_purge_valve;
  MockPump brew_pump, steam_pump;
  MockHeaterWithTolerance steam_heater_ctrl;
  EspressoMachine machine;

  machine.set_brew_valve(&brew_valve);
  machine.set_brew_purge_valve(&purge_valve);
  machine.set_brew_pump(&brew_pump);
  machine.set_brew_target_temperature(90.0f);
  machine.set_brew_flow_max(40.0f);
  machine.set_brew_flow_offset(20.0f);

  machine.set_steam_valve(&steam_valve);
  machine.set_steam_purge_valve(&steam_purge_valve);
  machine.set_steam_pump(&steam_pump);
  machine.set_steam_target_temperature(135.0f);
  machine.set_steam_flow_max(2.0f);
  machine.set_steam_cool_down_to(90.0f);
  machine.set_steam_heater_ctrl(&steam_heater_ctrl);

  steam_heater_ctrl.tolerance = 0.5f;
  steam_heater_ctrl.current_temp = 134.7f;  // within 0.5°C of 135.0°C

  g_mock_millis = 0;
  machine.setup();
  machine.machine_on();

  machine.steam_start();
  EXPECT_EQ(machine.get_steam_state(), SteamState::HEATING);

  machine.loop();  // HEATING → STEAMING (is_ready returns true at 134.7°C)
  EXPECT_EQ(machine.get_steam_state(), SteamState::STEAMING);
  EXPECT_TRUE(steam_valve.open_state);
  EXPECT_TRUE(steam_pump.running);
}

// Steam heating waits when temperature is below the tolerance band.
TEST(HeaterReadiness, SteamWaitsWhenTempBelowSteamToleranceBand) {
  MockValve brew_valve, purge_valve, steam_valve, steam_purge_valve;
  MockPump brew_pump, steam_pump;
  MockHeaterWithTolerance steam_heater_ctrl;
  EspressoMachine machine;

  machine.set_brew_valve(&brew_valve);
  machine.set_brew_purge_valve(&purge_valve);
  machine.set_brew_pump(&brew_pump);
  machine.set_brew_target_temperature(90.0f);
  machine.set_brew_flow_max(40.0f);
  machine.set_brew_flow_offset(20.0f);

  machine.set_steam_valve(&steam_valve);
  machine.set_steam_purge_valve(&steam_purge_valve);
  machine.set_steam_pump(&steam_pump);
  machine.set_steam_target_temperature(135.0f);
  machine.set_steam_flow_max(2.0f);
  machine.set_steam_cool_down_to(90.0f);
  machine.set_steam_heater_ctrl(&steam_heater_ctrl);

  steam_heater_ctrl.tolerance = 0.5f;
  steam_heater_ctrl.current_temp = 134.0f;  // 1.0°C below target — outside tolerance

  g_mock_millis = 0;
  machine.setup();
  machine.machine_on();

  machine.steam_start();
  machine.loop();  // should stay in HEATING
  EXPECT_EQ(machine.get_steam_state(), SteamState::HEATING);
  EXPECT_FALSE(steam_valve.open_state);
  EXPECT_FALSE(steam_pump.running);
}

// ---------------------------------------------------------------------------
// 🟠 P1-3 — Temperature surfing: apply computed setpoint to climate
// ---------------------------------------------------------------------------

// P1-3: At shot start (elapsed=0) the ramp setpoint is target + offset.
TEST(P1TempSurfing, TempSurfingAppliesFullOffsetAtShotStart) {
  BrewHeaterFixture f;
  f.machine.set_brew_temp_offset(5.0f);
  f.machine.set_brew_temp_ramp_time_ms(20000);

  f.brew_heater_ctrl.current_temp = 90.0f;
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING; first surfing tick fires here
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::BREWING);

  // At t=0, desired = 90 + 5*(1 - 0/20000) = 95°C
  // set_target_temperature is called at HEATING→BREWING and then on the
  // first BREWING tick in the same loop call; target should be ~95°C.
  EXPECT_GE(f.brew_heater_ctrl.target_temp, 90.0f);
  EXPECT_LE(f.brew_heater_ctrl.target_temp, 95.0f);
}

// P1-3: After the ramp time has elapsed the setpoint returns to target.
TEST(P1TempSurfing, TempSurfingAppliesTargetAfterRampTime) {
  BrewHeaterFixture f;
  f.machine.set_brew_temp_offset(5.0f);
  f.machine.set_brew_temp_ramp_time_ms(20000);

  f.brew_heater_ctrl.current_temp = 90.0f;
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING

  // Advance past ramp time
  g_mock_millis = 25000;
  f.machine.loop();  // BREWING tick: desired = 90°C (ramp complete)
  EXPECT_FLOAT_EQ(f.brew_heater_ctrl.target_temp, 90.0f);
}

// P1-3: Without heater controller, temperature surfing does not crash.
TEST(P1TempSurfing, TempSurfingWithoutHeaterControllerIsNoop) {
  OrchestratorFixture f;  // no brew heater ctrl wired
  f.machine.set_brew_temp_offset(5.0f);
  f.machine.set_brew_temp_ramp_time_ms(20000);
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING (no crash)
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::BREWING);
}

// ---------------------------------------------------------------------------
// 🟠 P1-5 — Shot stats as HA sensor entities
// ---------------------------------------------------------------------------

// P1-5: Shot stats are published to sensor entities when BREWING → DONE.
TEST(P1ShotStats, ShotStatSensorsPublishedOnDone) {
  OrchestratorFixture f;
  esphome::sensor::Sensor time_sensor, volume_sensor, yield_sensor;

  f.machine.set_last_shot_time_sensor(&time_sensor);
  f.machine.set_last_shot_volume_sensor(&volume_sensor);
  f.machine.set_last_shot_yield_sensor(&yield_sensor);

  f.machine.brew_start();
  f.machine.loop();           // HEATING → BREWING

  g_mock_millis = 25000;
  f.brew_pump.volume = 40.0f;
  f.machine.loop();           // BREWING → DONE — sensors published here

  // time sensor: ~25 s
  EXPECT_NEAR(time_sensor.state, 25.0f, 0.5f);
  // volume sensor: 40 ml
  EXPECT_FLOAT_EQ(volume_sensor.state, 40.0f);
  // yield sensor: 40 - 20 = 20 ml (offset is 20 ml)
  EXPECT_FLOAT_EQ(yield_sensor.state, 20.0f);
}

// P1-5: With no sensors wired, BREWING → DONE does not crash.
TEST(P1ShotStats, ShotStatsNoCrashWithoutSensors) {
  OrchestratorFixture f;  // no sensors wired
  f.machine.brew_start();
  f.machine.loop();          // HEATING → BREWING
  f.brew_pump.volume = 40.0f;
  f.machine.loop();          // BREWING → DONE (no crash)
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::DONE);
}

// ---------------------------------------------------------------------------
// Steam purge-before-steam sequence
//
// Scenario (from issue):
//   1. Steam start → heater raised to steam temperature
//   2. Wait for temperature to reach target (HEATING)
//   3. Pump through purge valve to clear water (PURGING)
//   4. Close purge valve, open steam valve (STEAMING)
//   5. Stop on exit conditions (steam_stop, timeout, over-temp)
// ---------------------------------------------------------------------------

// Fixture with steam purge enabled (2 ml purge volume).
struct SteamPurgeFixture {
  MockValve brew_valve;
  MockValve purge_valve;
  MockValve steam_valve;
  MockValve steam_purge_valve;
  MockPump brew_pump;
  MockPump steam_pump;
  EspressoMachine machine;

  SteamPurgeFixture() {
    machine.set_brew_valve(&brew_valve);
    machine.set_brew_purge_valve(&purge_valve);
    machine.set_brew_pump(&brew_pump);
    machine.set_brew_target_temperature(90.0f);
    machine.set_brew_flow_max(40.0f);
    machine.set_brew_flow_offset(20.0f);

    machine.set_steam_valve(&steam_valve);
    machine.set_steam_purge_valve(&steam_purge_valve);
    machine.set_steam_pump(&steam_pump);
    machine.set_steam_target_temperature(135.0f);
    machine.set_steam_flow_max(2.0f);
    machine.set_steam_cool_down_to(90.0f);
    machine.set_steam_purge_volume_ml(2.0f);

    g_mock_millis = 0;
    machine.setup();
    machine.machine_on();
  }
};

// Purge phase entered after HEATING when purge volume is configured.
TEST(SteamPurge, HeatingTransitionsToPurgingWhenPurgeVolumeSet) {
  SteamPurgeFixture f;
  f.machine.steam_start();
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::HEATING);
  f.machine.loop();  // HEATING → PURGING (no heater_ctrl → immediate)
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::PURGING);
}

// During PURGING the purge valve is open and the pump is running.
TEST(SteamPurge, PurgingOpensPurgeValveAndStartsPump) {
  SteamPurgeFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → PURGING
  EXPECT_TRUE(f.steam_purge_valve.open_state);
  EXPECT_TRUE(f.steam_pump.running);
}

// During PURGING the steam valve must remain closed.
TEST(SteamPurge, PurgingSteamValveRemainsClosedDuringPurge) {
  SteamPurgeFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → PURGING
  EXPECT_FALSE(f.steam_valve.open_state);
}

// PURGING stays in PURGING while volume is below the threshold.
TEST(SteamPurge, PurgingDoesNotTransitionBeforePurgeVolumeReached) {
  SteamPurgeFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → PURGING
  f.steam_pump.volume = 1.9f;  // just under 2 ml
  f.machine.loop();
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::PURGING);
  EXPECT_FALSE(f.steam_valve.open_state);
}

// PURGING → STEAMING when purge volume is reached.
TEST(SteamPurge, PurgingTransitionsToSteamingAtPurgeVolume) {
  SteamPurgeFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → PURGING
  f.steam_pump.volume = 2.0f;
  f.machine.loop();  // PURGING → STEAMING
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::STEAMING);
}

// On PURGING → STEAMING: purge valve closes, steam valve opens, pump keeps running.
TEST(SteamPurge, PurgingToSteamingClosesPurgeOpensSteamAndKeepsPump) {
  SteamPurgeFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → PURGING
  f.steam_pump.volume = 2.0f;
  f.machine.loop();  // PURGING → STEAMING
  EXPECT_FALSE(f.steam_purge_valve.open_state);
  EXPECT_TRUE(f.steam_valve.open_state);
  EXPECT_TRUE(f.steam_pump.running);
}

// The flow counter is reset when PURGING transitions to STEAMING so that
// flow tracking during STEAMING reflects steam-only volume.
TEST(SteamPurge, PurgeFlowCounterResetOnTransitionToSteaming) {
  SteamPurgeFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → PURGING
  f.steam_pump.volume = 2.0f;  // purge volume done
  f.machine.loop();  // PURGING → STEAMING (resets pump flow)
  // Flow counter should have been reset; the mock reports 0 after reset.
  EXPECT_FLOAT_EQ(f.steam_pump.volume, 0.0f);
}

// steam_stop() during PURGING cancels immediately (like during HEATING).
TEST(SteamPurge, SteamStopDuringPurgingCancelsImmediately) {
  SteamPurgeFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → PURGING
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::PURGING);
  f.machine.steam_stop();
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::IDLE);
}

// steam_stop() during PURGING closes all valves and stops the pump.
TEST(SteamPurge, SteamStopDuringPurgingStopsAllHardware) {
  SteamPurgeFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → PURGING (purge valve open, pump on)
  EXPECT_TRUE(f.steam_purge_valve.open_state);
  EXPECT_TRUE(f.steam_pump.running);
  f.machine.steam_stop();
  EXPECT_FALSE(f.steam_purge_valve.open_state);
  EXPECT_FALSE(f.steam_pump.running);
}

// machine_off() during PURGING cancels the sequence immediately.
TEST(SteamPurge, MachineOffDuringPurgingCancelsImmediately) {
  SteamPurgeFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → PURGING
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::PURGING);
  f.machine.machine_off();
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
  EXPECT_FALSE(f.machine.is_powered_on());
}

// Full steam scenario with purge: HEATING → PURGING → STEAMING → COOLING → IDLE.
TEST(SteamPurge, FullSteamSequenceWithPurge) {
  SteamPurgeFixture f;

  // Step 1: start steaming (heater setpoint NOT checked here — no heater_ctrl)
  f.machine.steam_start();
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::HEATING);
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::STEAMING);
  EXPECT_FALSE(f.steam_purge_valve.open_state);
  EXPECT_FALSE(f.steam_valve.open_state);
  EXPECT_FALSE(f.steam_pump.running);

  // Step 2: transition to PURGING (temperature immediately reached — no heater_ctrl)
  f.machine.loop();
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::PURGING);
  EXPECT_TRUE(f.steam_purge_valve.open_state);
  EXPECT_FALSE(f.steam_valve.open_state);  // steam valve still closed
  EXPECT_TRUE(f.steam_pump.running);

  // Step 3: purge volume reached → STEAMING
  f.steam_pump.volume = 2.0f;
  f.machine.loop();
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::STEAMING);
  EXPECT_FALSE(f.steam_purge_valve.open_state);  // purge valve closed
  EXPECT_TRUE(f.steam_valve.open_state);          // steam valve open
  EXPECT_TRUE(f.steam_pump.running);

  // Step 4: manually stop → COOLING
  f.machine.steam_stop();
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::COOLING);
  EXPECT_FALSE(f.steam_valve.open_state);
  EXPECT_TRUE(f.steam_purge_valve.open_state);  // purge opens for cool-down flush

  // Step 5: COOLING → CLEANUP → IDLE
  f.machine.loop();  // COOLING → CLEANUP
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::CLEANUP);
  f.machine.loop();  // CLEANUP → IDLE
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
  EXPECT_FALSE(f.steam_purge_valve.open_state);
}

// Purge volume of 0 (default) skips PURGING and goes directly to STEAMING.
TEST(SteamPurge, ZeroPurgeVolumeSkipsPurgingState) {
  OrchestratorFixture f;  // default purge_volume = 0
  f.machine.steam_start();
  f.machine.loop();  // HEATING → STEAMING (no PURGING with purge_volume=0)
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::STEAMING);
}

// ---------------------------------------------------------------------------
// Steam timeout (auto-stop safety)
// ---------------------------------------------------------------------------

// Fixture with a steam timeout configured (10 seconds).
struct SteamTimeoutFixture {
  static constexpr uint32_t kSteamTimeoutMs = 10000;

  MockValve brew_valve;
  MockValve purge_valve;
  MockValve steam_valve;
  MockValve steam_purge_valve;
  MockPump brew_pump;
  MockPump steam_pump;
  EspressoMachine machine;

  SteamTimeoutFixture() {
    machine.set_brew_valve(&brew_valve);
    machine.set_brew_purge_valve(&purge_valve);
    machine.set_brew_pump(&brew_pump);
    machine.set_brew_target_temperature(90.0f);
    machine.set_brew_flow_max(40.0f);
    machine.set_brew_flow_offset(20.0f);

    machine.set_steam_valve(&steam_valve);
    machine.set_steam_purge_valve(&steam_purge_valve);
    machine.set_steam_pump(&steam_pump);
    machine.set_steam_target_temperature(135.0f);
    machine.set_steam_flow_max(2.0f);
    machine.set_steam_cool_down_to(90.0f);
    machine.set_steam_timeout_ms(kSteamTimeoutMs);

    g_mock_millis = 0;
    machine.setup();
    machine.machine_on();
  }
};

// Steam auto-stops when the timeout elapses.
TEST(SteamTimeout, SteamTimesOutWhenTimeoutElapses) {
  SteamTimeoutFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → STEAMING (no purge configured)
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::STEAMING);

  // Advance time past the 10-second timeout
  g_mock_millis = SteamTimeoutFixture::kSteamTimeoutMs + 1000;
  f.machine.loop();  // timeout fires → COOLING
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::COOLING);
  EXPECT_FALSE(f.steam_valve.open_state);
  EXPECT_FALSE(f.steam_pump.running);
}

// Steam does NOT auto-stop before the timeout.
TEST(SteamTimeout, SteamDoesNotTimeOutBeforeTimeout) {
  SteamTimeoutFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → STEAMING
  g_mock_millis = SteamTimeoutFixture::kSteamTimeoutMs - 1000;
  f.machine.loop();
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::STEAMING);
}

// Timeout = 0 (default) means steam runs until manually stopped.
TEST(SteamTimeout, SteamTimeoutDisabledByDefault) {
  OrchestratorFixture f;  // no timeout configured (default 0)
  f.machine.steam_start();
  f.machine.loop();  // HEATING → STEAMING
  g_mock_millis = 600000;  // 10 minutes
  f.machine.loop();
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::STEAMING);
}

// ---------------------------------------------------------------------------
// Steam purge with heater controller: full temperature-gated sequence
// ---------------------------------------------------------------------------

// Fixture: heater_ctrl + purge volume configured.
struct SteamPurgeWithHeaterFixture {
  MockValve brew_valve;
  MockValve purge_valve;
  MockValve steam_valve;
  MockValve steam_purge_valve;
  MockPump brew_pump;
  MockPump steam_pump;
  MockHeaterCtrl heater_ctrl;
  EspressoMachine machine;

  SteamPurgeWithHeaterFixture() {
    machine.set_brew_valve(&brew_valve);
    machine.set_brew_purge_valve(&purge_valve);
    machine.set_brew_pump(&brew_pump);
    machine.set_brew_target_temperature(90.0f);
    machine.set_brew_flow_max(40.0f);
    machine.set_brew_flow_offset(20.0f);

    machine.set_steam_valve(&steam_valve);
    machine.set_steam_purge_valve(&steam_purge_valve);
    machine.set_steam_pump(&steam_pump);
    machine.set_steam_target_temperature(135.0f);
    machine.set_steam_flow_max(2.0f);
    machine.set_steam_cool_down_to(90.0f);
    machine.set_steam_heater_ctrl(&heater_ctrl);
    machine.set_steam_purge_volume_ml(2.0f);

    heater_ctrl.current_temp = 25.0f;  // Start cold

    g_mock_millis = 0;
    machine.setup();
    machine.machine_on();
  }
};

// With heater_ctrl: steam_start() sets heater to steam temperature and waits.
TEST(SteamPurgeWithHeater, SteamStartSetsHeaterAndWaitsForTemp) {
  SteamPurgeWithHeaterFixture f;
  f.machine.steam_start();
  EXPECT_FLOAT_EQ(f.heater_ctrl.target_temp, 135.0f);
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::HEATING);

  // Still below target — must not advance to PURGING yet.
  f.heater_ctrl.current_temp = 100.0f;
  f.machine.loop();
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::HEATING);
  EXPECT_FALSE(f.steam_purge_valve.open_state);
  EXPECT_FALSE(f.steam_pump.running);
}

// With heater_ctrl: HEATING → PURGING when temperature is reached.
TEST(SteamPurgeWithHeater, HeatingTransitionsToPurgingWhenTempReached) {
  SteamPurgeWithHeaterFixture f;
  f.machine.steam_start();
  f.heater_ctrl.current_temp = 135.0f;
  f.machine.loop();  // HEATING → PURGING
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::PURGING);
  EXPECT_TRUE(f.steam_purge_valve.open_state);
  EXPECT_FALSE(f.steam_valve.open_state);
  EXPECT_TRUE(f.steam_pump.running);
}

// With heater_ctrl: PURGING → STEAMING at purge volume, then manual stop works.
TEST(SteamPurgeWithHeater, FullSequenceWithHeaterGating) {
  SteamPurgeWithHeaterFixture f;
  f.machine.steam_start();

  // Heater reaches steam temperature
  f.heater_ctrl.current_temp = 135.0f;
  f.machine.loop();  // HEATING → PURGING
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::PURGING);

  // Purge volume reached
  f.steam_pump.volume = 2.0f;
  f.machine.loop();  // PURGING → STEAMING
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::STEAMING);
  EXPECT_FALSE(f.steam_purge_valve.open_state);
  EXPECT_TRUE(f.steam_valve.open_state);

  // Manual stop
  f.machine.steam_stop();
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::COOLING);
  EXPECT_FLOAT_EQ(f.heater_ctrl.target_temp, 90.0f);  // setpoint lowered

  // Cool down and cleanup
  f.heater_ctrl.current_temp = 90.0f;
  f.machine.loop();  // COOLING → CLEANUP
  f.machine.loop();  // CLEANUP → IDLE
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
  EXPECT_FALSE(f.steam_purge_valve.open_state);
}

// With heater_ctrl: steam_stop() during PURGING lowers heater setpoint to cool_down_to.
TEST(SteamPurgeWithHeater, SteamStopDuringPurgingResetsHeaterSetpoint) {
  SteamPurgeWithHeaterFixture f;
  f.machine.steam_start();
  f.heater_ctrl.current_temp = 135.0f;
  f.machine.loop();  // HEATING → PURGING

  int count_before = f.heater_ctrl.set_target_count;
  f.machine.steam_stop();
  EXPECT_GT(f.heater_ctrl.set_target_count, count_before);
  EXPECT_FLOAT_EQ(f.heater_ctrl.target_temp, 90.0f);
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
}

// ---------------------------------------------------------------------------
// P2-5: Safety — residual flow after pump stop
// ---------------------------------------------------------------------------

// When flow_max is reached the brew valve closes immediately. Even if the pump
// mock reports additional "residual" volume (piping pressure bleed-off after
// pump shutdown), the orchestrator must NOT re-open the valve.
TEST(Safety, ResidualFlowAfterStop) {
  OrchestratorFixture f;
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::BREWING);
  EXPECT_TRUE(f.brew_valve.open_state);

  // Flow reaches flow_max → pump stops and brew valve closes.
  f.brew_pump.volume = 40.0f;
  f.machine.loop();  // BREWING → DONE
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::DONE);
  EXPECT_FALSE(f.brew_pump.running);
  EXPECT_FALSE(f.brew_valve.open_state);

  // Simulate residual pressure: pump reports additional volume after stopping.
  // Orchestrator must NOT re-open the valve or restart the pump.
  f.brew_pump.volume = 45.0f;  // 5 mL of residual beyond flow_max
  f.machine.loop();  // DONE → CLEANUP (valve stays closed)
  EXPECT_FALSE(f.brew_valve.open_state);
  EXPECT_FALSE(f.brew_pump.running);

  f.machine.loop();  // CLEANUP → IDLE
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
  EXPECT_FALSE(f.brew_valve.open_state);
  EXPECT_FALSE(f.brew_pump.running);
}

// ---------------------------------------------------------------------------
// P2-2: espresso_machine.flush helper action
// ---------------------------------------------------------------------------

TEST(Flush, FlushStartsWhenIdle) {
  OrchestratorFixture f;
  f.machine.flush(50.0f);
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::FLUSHING);
  EXPECT_STREQ(f.machine.mode_name(), "flushing");
  EXPECT_TRUE(f.brew_pump.running);
  EXPECT_TRUE(f.purge_valve.open_state);
  EXPECT_FALSE(f.brew_valve.open_state);  // brew valve stays closed
}

TEST(Flush, FlushResetsPumpFlowCounter) {
  OrchestratorFixture f;
  f.brew_pump.volume = 20.0f;  // pre-existing volume
  f.machine.flush(50.0f);
  EXPECT_GT(f.brew_pump.reset_flow_count, 0);
}

TEST(Flush, FlushIgnoredWhenMachineIsOff) {
  OrchestratorFixture f;
  f.machine.machine_off();
  f.machine.flush(50.0f);
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
  EXPECT_FALSE(f.brew_pump.running);
}

TEST(Flush, FlushIgnoredWhenBrewing) {
  OrchestratorFixture f;
  f.machine.brew_start();
  f.machine.flush(50.0f);  // should be ignored
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::BREWING);
}

TEST(Flush, FlushIgnoredForZeroVolume) {
  OrchestratorFixture f;
  f.machine.flush(0.0f);
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
}

TEST(Flush, FlushCompletesAtTargetVolume) {
  OrchestratorFixture f;
  f.machine.flush(50.0f);
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::FLUSHING);

  // Pump volume reaches target → flush ends, purge valve closes, pump stops.
  f.brew_pump.volume = 50.0f;
  f.machine.loop();
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
  EXPECT_FALSE(f.brew_pump.running);
  EXPECT_FALSE(f.purge_valve.open_state);
}

TEST(Flush, FlushDoesNotTerminateBeforeTarget) {
  OrchestratorFixture f;
  f.machine.flush(50.0f);

  f.brew_pump.volume = 49.9f;
  f.machine.loop();
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::FLUSHING);
  EXPECT_TRUE(f.brew_pump.running);
}

TEST(Flush, MachineOffDuringFlushStopsImmediately) {
  OrchestratorFixture f;
  f.machine.flush(50.0f);
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::FLUSHING);

  f.machine.machine_off();
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
  EXPECT_FALSE(f.brew_pump.running);
  EXPECT_FALSE(f.purge_valve.open_state);
}

// ---------------------------------------------------------------------------
// P2-1: Cleanup callback fired in brew DONE → CLEANUP transition
// ---------------------------------------------------------------------------

TEST(CleanupCallback, BrewCleanupFnCalledAtDone) {
  OrchestratorFixture f;
  int called = 0;
  f.machine.set_brew_cleanup_fn([&called]() { called++; });

  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING
  f.brew_pump.volume = 40.0f;
  f.machine.loop();  // BREWING → DONE (fn not yet called; state set to DONE)
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::DONE);
  EXPECT_EQ(called, 0);  // fn fires when DONE state is processed, not when entered

  f.machine.loop();  // DONE → CLEANUP (cleanup fn fires here)
  EXPECT_EQ(called, 1);
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::CLEANUP);
}

TEST(CleanupCallback, BrewCleanupFnNotCalledWhenNotSet) {
  OrchestratorFixture f;
  // No cleanup fn set — should not crash.
  f.machine.brew_start();
  f.machine.loop();
  f.brew_pump.volume = 40.0f;
  f.machine.loop();  // BREWING → DONE
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::DONE);
}

TEST(CleanupCallback, SteamCleanupFnCalledAtCleanup) {
  OrchestratorFixture f;
  int called = 0;
  f.machine.set_steam_cleanup_fn([&called]() { called++; });

  f.machine.steam_start();
  f.machine.loop();  // HEATING → STEAMING
  f.machine.steam_stop();  // STEAMING → COOLING
  f.machine.loop();  // COOLING → CLEANUP (fn not yet called; state set to CLEANUP)
  EXPECT_EQ(called, 0);

  f.machine.loop();  // CLEANUP → IDLE (cleanup fn fires here)
  EXPECT_EQ(called, 1);
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
}

// ---------------------------------------------------------------------------
// status_name() — verbose status strings including live sensor values
// ---------------------------------------------------------------------------

TEST(StatusName, IdleReturnsIdle) {
  OrchestratorFixture f;
  EXPECT_EQ(f.machine.status_name(), "Idle");
}

TEST(StatusName, BrewHeatingStatus) {
  OrchestratorFixture f;
  f.machine.brew_start();
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::HEATING);
  // No heater_ctrl wired → shows target only
  EXPECT_EQ(f.machine.status_name(), "Heating to 90.0°C");
}

TEST(StatusName, BrewHeatingStatusWithHeaterCtrl) {
  OrchestratorFixture f;
  struct WarmHeater : public IHeater {
    float get_current_temperature() const override { return 85.3f; }
    void set_target_temperature(float) override {}
  } heater;
  f.machine.set_brew_heater_ctrl(&heater);
  f.machine.brew_start();
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::HEATING);
  // With heater_ctrl → shows target and current
  EXPECT_EQ(f.machine.status_name(), "Heating to 90.0°C (now 85.3°C)");
}

TEST(StatusName, BrewingStatus) {
  OrchestratorFixture f;
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING (no heater_ctrl wired)
  f.brew_pump.volume = 15.2f;
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::BREWING);
  EXPECT_EQ(f.machine.status_name(), "Brewing: 15.2 ml / 40.0 ml");
}

TEST(StatusName, BrewDoneStatus) {
  OrchestratorFixture f;
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING
  f.brew_pump.volume = 40.0f;
  f.machine.loop();  // BREWING → DONE
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::DONE);
  // last_shot_time_s_ ≈ 0 in test (millis() returns 0)
  EXPECT_EQ(f.machine.status_name(), "Shot done: 40.0 ml in 0.0 s");
}

TEST(StatusName, BrewCleanupStatus) {
  OrchestratorFixture f;
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING
  f.brew_pump.volume = 40.0f;
  f.machine.loop();  // BREWING → DONE
  f.machine.loop();  // DONE → CLEANUP
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::CLEANUP);
  EXPECT_EQ(f.machine.status_name(), "Brew cleanup");
}

TEST(StatusName, SteamHeatingStatus) {
  OrchestratorFixture f;
  // Wire a heater controller that is not yet at target temperature
  struct ColdHeater : public IHeater {
    float get_current_temperature() const override { return 80.0f; }
    void set_target_temperature(float) override {}
  } cold_heater;
  f.machine.set_steam_heater_ctrl(&cold_heater);
  f.machine.steam_start();
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::HEATING);
  EXPECT_EQ(f.machine.status_name(), "Heating to steam 135.0°C (now 80.0°C)");
}

TEST(StatusName, SteamHeatingStatusNoCtrl) {
  OrchestratorFixture f;
  f.machine.steam_start();
  // loop() would transition immediately to STEAMING without heater_ctrl
  // Check status while still in HEATING (before loop)
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::HEATING);
  EXPECT_EQ(f.machine.status_name(), "Heating to steam 135.0°C");
}

TEST(StatusName, SteamingStatus) {
  OrchestratorFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → STEAMING (no heater_ctrl wired)
  f.steam_pump.rate = 1.8f;
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::STEAMING);
  EXPECT_EQ(f.machine.status_name(), "Steaming: 1.8 ml/s");
}

TEST(StatusName, SteamCoolingStatus) {
  OrchestratorFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → STEAMING
  f.machine.steam_stop();  // STEAMING → COOLING
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::COOLING);
  // No heater_ctrl → shows target only
  EXPECT_EQ(f.machine.status_name(), "Cooling to 90.0°C");
}

TEST(StatusName, SteamCoolingStatusWithHeaterCtrl) {
  OrchestratorFixture f;
  struct MutableHeater : public IHeater {
    float temp = 140.0f;  // above steam target → HEATING transitions immediately
    float get_current_temperature() const override { return temp; }
    void set_target_temperature(float) override {}
  } heater;
  f.machine.set_steam_heater_ctrl(&heater);
  f.machine.steam_start();
  f.machine.loop();  // HEATING → STEAMING (140.0 >= 135.0)
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::STEAMING);
  heater.temp = 125.3f;  // simulate temp dropping after steam stops
  f.machine.steam_stop();  // STEAMING → COOLING
  EXPECT_EQ(f.machine.get_steam_state(), SteamState::COOLING);
  EXPECT_EQ(f.machine.status_name(), "Cooling to 90.0°C (now 125.3°C)");
}

TEST(StatusName, FlushingStatus) {
  OrchestratorFixture f;
  f.machine.flush(30.0f);
  f.brew_pump.volume = 12.3f;
  EXPECT_EQ(f.machine.status_name(), "Flushing: 12.3 ml / 30.0 ml");
}

// ---------------------------------------------------------------------------
// status_sensor — publishes updated status on state transitions and loop ticks
// ---------------------------------------------------------------------------

TEST(StatusSensor, PublishesOnBrewStart) {
  OrchestratorFixture f;
  esphome::text_sensor::TextSensor sens;
  f.machine.set_status_sensor(&sens);
  f.machine.brew_start();
  EXPECT_EQ(sens.state, "Heating to 90.0°C");
}

TEST(StatusSensor, PublishesOnBrewingTransition) {
  OrchestratorFixture f;
  esphome::text_sensor::TextSensor sens;
  f.machine.set_status_sensor(&sens);
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING; loop() also calls publish_status_()
  EXPECT_EQ(sens.state, "Brewing: 0.0 ml / 40.0 ml");
}

TEST(StatusSensor, UpdatesLiveAsFlowIncreases) {
  OrchestratorFixture f;
  esphome::text_sensor::TextSensor sens;
  f.machine.set_status_sensor(&sens);
  f.machine.brew_start();
  f.machine.loop();  // → BREWING

  f.brew_pump.volume = 10.0f;
  f.machine.loop();  // status should update
  EXPECT_EQ(sens.state, "Brewing: 10.0 ml / 40.0 ml");

  f.brew_pump.volume = 25.0f;
  f.machine.loop();
  EXPECT_EQ(sens.state, "Brewing: 25.0 ml / 40.0 ml");
}

TEST(StatusSensor, PublishesOnBrewStop) {
  OrchestratorFixture f;
  esphome::text_sensor::TextSensor sens;
  f.machine.set_status_sensor(&sens);
  f.machine.brew_start();
  f.machine.brew_stop();
  EXPECT_EQ(sens.state, "Idle");
}

TEST(StatusSensor, PublishesOnSteamStart) {
  OrchestratorFixture f;
  esphome::text_sensor::TextSensor sens;
  f.machine.set_status_sensor(&sens);
  f.machine.steam_start();
  EXPECT_EQ(sens.state, "Heating to steam 135.0°C");
}

TEST(StatusSensor, PublishesOnFlush) {
  OrchestratorFixture f;
  esphome::text_sensor::TextSensor sens;
  f.machine.set_status_sensor(&sens);
  f.machine.flush(20.0f);
  EXPECT_EQ(sens.state, "Flushing: 0.0 ml / 20.0 ml");
}

TEST(StatusSensor, PublishesIdleAfterFlushComplete) {
  OrchestratorFixture f;
  esphome::text_sensor::TextSensor sens;
  f.machine.set_status_sensor(&sens);
  f.machine.flush(20.0f);
  f.brew_pump.volume = 20.0f;
  f.machine.loop();  // Flush done → IDLE; loop() publishes final state
  EXPECT_EQ(sens.state, "Idle");
}

TEST(StatusSensor, DeduplicatesIdenticalUpdates) {
  OrchestratorFixture f;
  esphome::text_sensor::TextSensor sens;
  int publish_count = 0;
  // Wrap the TextSensor to count publishes
  struct CountingSensor : public esphome::text_sensor::TextSensor {
    int *count;
    void publish_state(const std::string &v) {
      (*count)++;
      esphome::text_sensor::TextSensor::publish_state(v);
    }
  } counting_sens;
  counting_sens.count = &publish_count;

  f.machine.set_status_sensor(&counting_sens);
  f.machine.brew_start();           // 1 publish: "Heating to 90.0°C"
  int after_start = publish_count;
  // Multiple loop ticks while still HEATING → value doesn't change → no re-publish
  f.machine.loop();  // HEATING → BREWING and publish_status_ in loop → 1 more publish
  f.machine.loop();  // pump still 0 → "Brewing: 0.0 ml / 40.0 ml" already published
  EXPECT_EQ(publish_count, after_start + 1);  // only 1 extra for BREWING state entry
}
