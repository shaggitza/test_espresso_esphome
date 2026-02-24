#include <gtest/gtest.h>
#include <limits>
#include "esphome/core/hal.h"
#include "esphome/components/sensor/sensor.h"
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
    machine.machine_on();  // power on so brew/steam actions are accepted
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
