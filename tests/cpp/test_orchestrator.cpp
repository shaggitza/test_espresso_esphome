#include <gtest/gtest.h>
#include "esphome/core/hal.h"
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

TEST(Orchestrator, SteamPumpDutyCycleTurnsOffAtTargetFlowRate) {
  OrchestratorFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → STEAMING (pump on)
  EXPECT_TRUE(f.steam_pump.running);

  // Simulate flow rate reaching target
  f.steam_pump.rate = 2.0f;
  f.machine.loop();  // STEAMING: rate >= target → pump off
  EXPECT_FALSE(f.steam_pump.running);
}

TEST(Orchestrator, SteamPumpDutyCycleTurnsOnBelowTargetFlowRate) {
  OrchestratorFixture f;
  f.machine.steam_start();
  f.machine.loop();  // HEATING → STEAMING (pump on)

  // Simulate flow above target so pump turns off
  f.steam_pump.rate = 2.0f;
  f.machine.loop();  // STEAMING: rate >= target → pump off
  EXPECT_FALSE(f.steam_pump.running);

  // Drop rate below target
  f.steam_pump.rate = 1.5f;
  f.machine.loop();  // STEAMING: rate < target → pump on
  EXPECT_TRUE(f.steam_pump.running);
}

TEST(Orchestrator, SteamPumpRunsContinuouslyWithoutFlowMeter) {
  OrchestratorFixture f;
  // steam_pump.rate defaults to 0.0f (no flow meter), target is 2.0f
  f.machine.steam_start();
  f.machine.loop();  // HEATING → STEAMING (pump on)
  f.machine.loop();  // STEAMING: rate(0) < target(2) → pump stays on
  EXPECT_TRUE(f.steam_pump.running);
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
  }
};

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
