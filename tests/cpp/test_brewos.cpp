// Unit tests for the BrewOS connector component.
//
// These tests exercise the two platform-independent code paths:
//   1. on_message() — command dispatch from cloud JSON to EspressoMachine
//   2. build_status_json() — status serialisation for cloud status messages
//
// The WebSocket networking layer (guarded by #ifdef ARDUINO) is not tested
// here; it requires real Arduino hardware.

#include <gtest/gtest.h>
#include "espresso_machine_brewos/brewos_connector.h"
#include "espresso_machine/espresso_machine.h"
#include "espresso_machine/interfaces.h"

using namespace esphome::espresso_machine_brewos;
using namespace esphome::espresso_machine;

extern uint32_t g_mock_millis;

// ---------------------------------------------------------------------------
// Minimal in-test doubles — same pattern as test_orchestrator.cpp
// ---------------------------------------------------------------------------
struct BValve : public IValve {
  bool open_state = false;
  void open() override { open_state = true; }
  void close() override { open_state = false; }
  bool is_open() const override { return open_state; }
};

struct BPump : public IPump {
  bool running = false;
  float volume = 0.0f;
  float rate = 0.0f;
  void turn_on() override { running = true; }
  void turn_off() override { running = false; }
  bool is_running() const override { return running; }
  float get_flow_rate() const override { return rate; }
  float get_flow_total() const override { return volume; }
  void reset_flow() override {
    volume = 0.0f;
    rate = 0.0f;
  }
};

// ---------------------------------------------------------------------------
// Test fixture — wires a BrewOSConnector to a fully configured EspressoMachine
// ---------------------------------------------------------------------------
struct BrewOSFixture {
  BValve brew_valve;
  BValve purge_valve;
  BValve steam_valve;
  BValve steam_purge_valve;
  BPump brew_pump;
  BPump steam_pump;
  EspressoMachine machine;
  BrewOSConnector connector;

  BrewOSFixture() {
    // Minimal brew configuration
    machine.set_brew_valve(&brew_valve);
    machine.set_brew_purge_valve(&purge_valve);
    machine.set_brew_pump(&brew_pump);
    machine.set_brew_target_temperature(90.0f);
    machine.set_brew_flow_max(40.0f);
    machine.set_brew_flow_offset(20.0f);

    // Minimal steam configuration
    machine.set_steam_valve(&steam_valve);
    machine.set_steam_purge_valve(&steam_purge_valve);
    machine.set_steam_pump(&steam_pump);
    machine.set_steam_target_temperature(135.0f);
    machine.set_steam_flow_max(2.0f);
    machine.set_steam_cool_down_to(90.0f);

    machine.setup();

    connector.set_url("https://cloud.brewos.io");
    connector.set_espresso_machine(&machine);
    // setup() is a no-op outside Arduino; skip it here.
  }
};

// ---------------------------------------------------------------------------
// build_status_json() tests
// ---------------------------------------------------------------------------

TEST(BrewOSStatusJson, ContainsPicoStatusType) {
  BrewOSFixture f;
  std::string json = f.connector.build_status_json();
  EXPECT_NE(json.find("\"type\":\"pico_status\""), std::string::npos)
      << "Expected pico_status type in: " << json;
}

TEST(BrewOSStatusJson, IdleModeWhenNotPoweredOn) {
  BrewOSFixture f;
  std::string json = f.connector.build_status_json();
  EXPECT_NE(json.find("\"mode\":\"idle\""), std::string::npos)
      << "Expected idle mode in: " << json;
}

TEST(BrewOSStatusJson, PoweredOnReflected) {
  BrewOSFixture f;
  f.machine.machine_on();
  std::string json = f.connector.build_status_json();
  EXPECT_NE(json.find("\"powered_on\":true"), std::string::npos)
      << "Expected powered_on:true in: " << json;
}

TEST(BrewOSStatusJson, IsNotBusyWhenIdle) {
  BrewOSFixture f;
  std::string json = f.connector.build_status_json();
  EXPECT_NE(json.find("\"is_busy\":false"), std::string::npos)
      << "Expected is_busy:false in: " << json;
}

TEST(BrewOSStatusJson, IsBusyDuringBrew) {
  BrewOSFixture f;
  f.machine.machine_on();
  f.machine.brew_start();
  std::string json = f.connector.build_status_json();
  EXPECT_NE(json.find("\"is_busy\":true"), std::string::npos)
      << "Expected is_busy:true during brew in: " << json;
}

// ---------------------------------------------------------------------------
// on_message() — machine_on / machine_off
// ---------------------------------------------------------------------------

TEST(BrewOSCommand, MachineOn) {
  BrewOSFixture f;
  ASSERT_FALSE(f.machine.is_powered_on());
  f.connector.on_message("{\"type\":\"machine_on\"}");
  EXPECT_TRUE(f.machine.is_powered_on());
}

TEST(BrewOSCommand, MachineOff) {
  BrewOSFixture f;
  f.machine.machine_on();
  ASSERT_TRUE(f.machine.is_powered_on());
  f.connector.on_message("{\"type\":\"machine_off\"}");
  EXPECT_FALSE(f.machine.is_powered_on());
}

// ---------------------------------------------------------------------------
// on_message() — brew_start / brew_stop
// ---------------------------------------------------------------------------

TEST(BrewOSCommand, BrewStart) {
  BrewOSFixture f;
  f.machine.machine_on();
  ASSERT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
  f.connector.on_message("{\"type\":\"brew_start\"}");
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::BREWING);
}

TEST(BrewOSCommand, BrewStop) {
  BrewOSFixture f;
  f.machine.machine_on();
  f.machine.brew_start();
  ASSERT_EQ(f.machine.get_mode(), EspressoMode::BREWING);
  f.connector.on_message("{\"type\":\"brew_stop\"}");
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
}

// ---------------------------------------------------------------------------
// on_message() — steam_start / steam_stop
// ---------------------------------------------------------------------------

TEST(BrewOSCommand, SteamStart) {
  BrewOSFixture f;
  f.machine.machine_on();
  ASSERT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
  f.connector.on_message("{\"type\":\"steam_start\"}");
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::STEAMING);
}

TEST(BrewOSCommand, SteamStop) {
  BrewOSFixture f;
  f.machine.machine_on();
  f.machine.steam_start();
  ASSERT_EQ(f.machine.get_mode(), EspressoMode::STEAMING);
  f.connector.on_message("{\"type\":\"steam_stop\"}");
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
}

// ---------------------------------------------------------------------------
// on_message() — flush
// ---------------------------------------------------------------------------

TEST(BrewOSCommand, FlushAccepted) {
  BrewOSFixture f;
  f.machine.machine_on();
  ASSERT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
  f.connector.on_message("{\"type\":\"flush\",\"volume_ml\":30}");
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::FLUSHING);
}

TEST(BrewOSCommand, FlushZeroVolumeIgnored) {
  BrewOSFixture f;
  f.machine.machine_on();
  f.connector.on_message("{\"type\":\"flush\",\"volume_ml\":0}");
  // Zero-volume flush should be silently ignored; machine stays IDLE.
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
}

// ---------------------------------------------------------------------------
// on_message() — edge cases
// ---------------------------------------------------------------------------

TEST(BrewOSCommand, UnknownTypeIgnored) {
  BrewOSFixture f;
  f.machine.machine_on();
  // Should not throw or crash; machine remains IDLE.
  f.connector.on_message("{\"type\":\"set_grind_time\",\"value\":7}");
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
}

TEST(BrewOSCommand, MissingTypeIgnored) {
  BrewOSFixture f;
  f.machine.machine_on();
  f.connector.on_message("{\"value\":42}");
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
}

TEST(BrewOSCommand, EmptyMessageIgnored) {
  BrewOSFixture f;
  // Must not crash.
  f.connector.on_message("");
  EXPECT_EQ(f.machine.get_mode(), EspressoMode::IDLE);
}

TEST(BrewOSCommand, NullMachineNocrash) {
  // Connector with no machine wired — must not crash on any message.
  BrewOSConnector connector;
  connector.on_message("{\"type\":\"brew_start\"}");
  // No assertions needed; test passes if no crash.
}

// ---------------------------------------------------------------------------
// is_connected() — default state
// ---------------------------------------------------------------------------

TEST(BrewOSConnection, DefaultNotConnected) {
  BrewOSConnector connector;
  EXPECT_FALSE(connector.is_connected());
}
