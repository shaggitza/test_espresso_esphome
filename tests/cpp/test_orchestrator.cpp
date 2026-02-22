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

  void turn_on() override {
    running = true;
    on_count++;
  }
  void turn_off() override {
    running = false;
    off_count++;
  }
  bool is_running() const override { return running; }
};

struct MockFlowMeter : public IFlowMeter {
  float volume = 0.0f;
  float rate = 0.0f;
  int reset_count = 0;

  float get_rate() const override { return rate; }
  float get_total_volume() const override { return volume; }
  void reset() override {
    volume = 0.0f;
    rate = 0.0f;
    reset_count++;
  }
};

// ---------------------------------------------------------------------------
// Helper to build a fully-wired orchestrator
// ---------------------------------------------------------------------------
struct OrchestratorFixture {
  MockValve brew_valve;
  MockValve purge_valve;
  MockValve steam_valve;
  MockValve steam_purge_valve;
  MockPump brew_pump;
  MockPump steam_pump;
  MockFlowMeter flow_meter;
  EspressoMachine machine;

  OrchestratorFixture() {
    machine.set_brew_valve(&brew_valve);
    machine.set_brew_purge_valve(&purge_valve);
    machine.set_brew_pump(&brew_pump);
    machine.set_brew_flow_meter(&flow_meter);
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
  f.flow_meter.volume = 15.0f;
  f.machine.brew_start();
  EXPECT_EQ(f.flow_meter.reset_count, 1);
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

  // Set flow meter to the trigger volume
  f.flow_meter.volume = 40.0f;
  f.machine.loop();  // BREWING → DONE
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::DONE);
  EXPECT_FALSE(f.brew_pump.running);
  EXPECT_FALSE(f.brew_valve.open_state);
}

TEST(Orchestrator, BrewDoesNotTerminateBeforeFlowMax) {
  OrchestratorFixture f;
  f.machine.brew_start();
  f.machine.loop();  // HEATING → BREWING

  f.flow_meter.volume = 39.9f;
  f.machine.loop();  // still BREWING
  EXPECT_EQ(f.machine.get_brew_state(), BrewState::BREWING);
  EXPECT_TRUE(f.brew_pump.running);
}

TEST(Orchestrator, BrewDoneTransitionsToIdle) {
  OrchestratorFixture f;
  f.machine.brew_start();
  f.machine.loop();          // HEATING → BREWING
  f.flow_meter.volume = 40.0f;
  f.machine.loop();          // BREWING → DONE
  f.machine.loop();          // DONE → IDLE
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
  f.machine.steam_stop();
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
  f.machine.steam_stop();
  EXPECT_FALSE(f.machine.is_busy());
}
