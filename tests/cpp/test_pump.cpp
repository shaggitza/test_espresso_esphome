#include <gtest/gtest.h>
#include "esphome/core/hal.h"
#include "espresso_machine_pump/pump.h"

using namespace esphome::espresso_machine_pump;
using namespace esphome::espresso_machine;

extern uint32_t g_mock_millis;

// ---------------------------------------------------------------------------
// Minimal IFlowMeter stub for pump tests
// ---------------------------------------------------------------------------
struct MockFlowMeter : public IFlowMeter {
  float volume{0.0f};
  float rate{0.0f};
  int reset_count{0};

  float get_rate() const override { return rate; }
  float get_total_volume() const override { return volume; }
  void reset() override {
    volume = 0.0f;
    rate = 0.0f;
    reset_count++;
  }
};

// ---------------------------------------------------------------------------
// PumpSwitch (relay) — basic on/off behaviour
// ---------------------------------------------------------------------------

static PumpSwitch make_pump(GPIOPin &pin) {
  PumpSwitch p;
  p.set_pin(&pin);
  p.set_min_on_ms(0);   // disable timing constraints for basic tests
  p.set_min_off_ms(0);
  p.setup();
  return p;
}

TEST(PumpSwitch, InitiallyOffAfterSetup) {
  GPIOPin pin;
  PumpSwitch p = make_pump(pin);
  EXPECT_FALSE(p.is_running());
  EXPECT_FALSE(pin.state_);
  EXPECT_FALSE(p.state);  // switch entity reports OFF to HA
}

TEST(PumpSwitch, TurnOnSetsHighPin) {
  GPIOPin pin;
  PumpSwitch p = make_pump(pin);
  p.turn_on();
  EXPECT_TRUE(p.is_running());
  EXPECT_TRUE(pin.state_);
  EXPECT_TRUE(p.state);
}

TEST(PumpSwitch, TurnOffSetsLowPin) {
  GPIOPin pin;
  PumpSwitch p = make_pump(pin);
  p.turn_on();
  p.turn_off();
  EXPECT_FALSE(p.is_running());
  EXPECT_FALSE(pin.state_);
  EXPECT_FALSE(p.state);
}

TEST(PumpSwitch, TurnOnIsIdempotent) {
  GPIOPin pin;
  PumpSwitch p = make_pump(pin);
  p.turn_on();
  p.turn_on();
  EXPECT_TRUE(p.is_running());
  EXPECT_TRUE(pin.state_);
}

TEST(PumpSwitch, TurnOffIsIdempotent) {
  GPIOPin pin;
  PumpSwitch p = make_pump(pin);
  p.turn_off();
  p.turn_off();
  EXPECT_FALSE(p.is_running());
  EXPECT_FALSE(pin.state_);
}

// ---------------------------------------------------------------------------
// Volume-driven run — flow meter wired
// ---------------------------------------------------------------------------

TEST(PumpSwitch, RunResetsFlowAndStartsPump) {
  GPIOPin pin;
  MockFlowMeter fm;
  fm.volume = 5.0f;  // pre-existing volume

  PumpSwitch p = make_pump(pin);
  p.set_flow_meter(&fm);

  p.run(40.0f);
  EXPECT_TRUE(p.is_running());
  EXPECT_EQ(fm.reset_count, 1);   // flow counter was reset
  EXPECT_FLOAT_EQ(fm.volume, 0.0f);
}

TEST(PumpSwitch, RunStopsWhenTargetVolumeReached) {
  GPIOPin pin;
  MockFlowMeter fm;
  PumpSwitch p = make_pump(pin);
  p.set_flow_meter(&fm);

  p.run(40.0f);
  EXPECT_TRUE(p.is_running());

  // Still running just below target
  fm.volume = 39.9f;
  p.loop();
  EXPECT_TRUE(p.is_running());

  // At target — loop() should auto-stop
  fm.volume = 40.0f;
  p.loop();
  EXPECT_FALSE(p.is_running());
  EXPECT_FALSE(pin.state_);
}

TEST(PumpSwitch, RunDoesNotStopBelowTarget) {
  GPIOPin pin;
  MockFlowMeter fm;
  PumpSwitch p = make_pump(pin);
  p.set_flow_meter(&fm);

  p.run(40.0f);
  fm.volume = 39.9f;
  p.loop();
  EXPECT_TRUE(p.is_running());
}

// ---------------------------------------------------------------------------
// Volume-driven run — no flow meter (runs until external stop)
// ---------------------------------------------------------------------------

TEST(PumpSwitch, RunWithoutFlowMeterRunsIndefinitely) {
  GPIOPin pin;
  g_mock_millis = 0;
  PumpSwitch p = make_pump(pin);
  // No flow meter set

  p.run(40.0f);  // target volume requested, but no meter to measure it
  EXPECT_TRUE(p.is_running());

  // Simulate many loop() calls — pump must stay on without a flow meter
  for (int i = 0; i < 100; i++)
    p.loop();
  EXPECT_TRUE(p.is_running());

  // External stop still works
  p.turn_off();
  EXPECT_FALSE(p.is_running());
}

// ---------------------------------------------------------------------------
// Safety timeout
// ---------------------------------------------------------------------------

TEST(PumpSwitch, RunSafetyTimeoutStopsPump) {
  GPIOPin pin;
  MockFlowMeter fm;
  g_mock_millis = 0;
  PumpSwitch p = make_pump(pin);
  p.set_flow_meter(&fm);

  // 500 ms safety cap; flow meter never increments (simulates stall)
  p.run(40.0f, 500);
  EXPECT_TRUE(p.is_running());

  // Before timeout
  g_mock_millis = 499;
  p.loop();
  EXPECT_TRUE(p.is_running());

  // At timeout — loop() should stop
  g_mock_millis = 500;
  p.loop();
  EXPECT_FALSE(p.is_running());
  EXPECT_FALSE(pin.state_);
}

TEST(PumpSwitch, RunCanBeStoppedExternallyBeforeTarget) {
  GPIOPin pin;
  MockFlowMeter fm;
  PumpSwitch p = make_pump(pin);
  p.set_flow_meter(&fm);

  p.run(40.0f);
  EXPECT_TRUE(p.is_running());

  p.turn_off();  // stop button / temp safety / etc.
  EXPECT_FALSE(p.is_running());

  // loop() must not restart it
  fm.volume = 0.0f;
  p.loop();
  EXPECT_FALSE(p.is_running());
}

// ---------------------------------------------------------------------------
// Minimum on-time — pump-level hardware constraint
// ---------------------------------------------------------------------------

// turn_off() must be silently ignored while min_on_ms has not yet elapsed.
TEST(PumpSwitchMinOnTime, TurnOffIgnoredDuringMinOnWindow) {
  GPIOPin pin;
  g_mock_millis = 0;
  PumpSwitch p = make_pump(pin);
  p.set_min_on_ms(500);

  p.turn_on();
  EXPECT_TRUE(p.is_running());

  // Below min_on_ms — turn_off() must be a no-op.
  g_mock_millis = 250;
  p.turn_off();
  EXPECT_TRUE(p.is_running());

  g_mock_millis = 499;
  p.turn_off();
  EXPECT_TRUE(p.is_running());
}

// turn_off() succeeds once min_on_ms has elapsed.
TEST(PumpSwitchMinOnTime, TurnOffSucceedsAfterMinOnWindow) {
  GPIOPin pin;
  g_mock_millis = 0;
  PumpSwitch p = make_pump(pin);
  p.set_min_on_ms(500);

  p.turn_on();
  g_mock_millis = 500;
  p.turn_off();
  EXPECT_FALSE(p.is_running());
  EXPECT_FALSE(pin.state_);
}

// With min_on_ms == 0 (disabled), turn_off() succeeds immediately.
TEST(PumpSwitchMinOnTime, ZeroMinOnAllowsImmediateTurnOff) {
  GPIOPin pin;
  g_mock_millis = 0;
  PumpSwitch p = make_pump(pin);
  p.set_min_on_ms(0);

  p.turn_on();
  p.turn_off();
  EXPECT_FALSE(p.is_running());
}

// First turn_on() is always allowed even when default min_on_ms is set.
TEST(PumpSwitchMinOnTime, FirstTurnOnAlwaysAllowed) {
  GPIOPin pin;
  g_mock_millis = 0;
  PumpSwitch p = make_pump(pin);
  p.set_min_on_ms(500);
  // Never been on — first turn_on() must succeed immediately.
  p.turn_on();
  EXPECT_TRUE(p.is_running());
}

// ---------------------------------------------------------------------------
// Minimum off-time — pump-level hardware constraint
// ---------------------------------------------------------------------------

// turn_on() must be silently ignored while min_off_ms has not yet elapsed.
TEST(PumpSwitchMinOffTime, TurnOnIgnoredDuringMinOffWindow) {
  GPIOPin pin;
  g_mock_millis = 0;
  PumpSwitch p = make_pump(pin);
  p.set_min_on_ms(0);  // disable min_on to test min_off in isolation
  p.set_min_off_ms(500);

  p.turn_on();
  p.turn_off();
  EXPECT_FALSE(p.is_running());

  // Below min_off_ms — turn_on() must be a no-op.
  g_mock_millis = 250;
  p.turn_on();
  EXPECT_FALSE(p.is_running());

  g_mock_millis = 499;
  p.turn_on();
  EXPECT_FALSE(p.is_running());
}

// turn_on() succeeds once min_off_ms has elapsed.
TEST(PumpSwitchMinOffTime, TurnOnSucceedsAfterMinOffWindow) {
  GPIOPin pin;
  g_mock_millis = 0;
  PumpSwitch p = make_pump(pin);
  p.set_min_on_ms(0);
  p.set_min_off_ms(500);

  p.turn_on();
  p.turn_off();
  g_mock_millis = 500;
  p.turn_on();
  EXPECT_TRUE(p.is_running());
  EXPECT_TRUE(pin.state_);
}

// With min_off_ms == 0 (default/disabled), turn_on() succeeds immediately after off.
TEST(PumpSwitchMinOffTime, ZeroMinOffAllowsImmediateTurnOn) {
  GPIOPin pin;
  g_mock_millis = 0;
  PumpSwitch p = make_pump(pin);
  p.set_min_on_ms(0);
  p.set_min_off_ms(0);

  p.turn_on();
  p.turn_off();
  p.turn_on();
  EXPECT_TRUE(p.is_running());
}

// First turn_on() is never gated by min_off_ms (pump starts in "never been off" state).
TEST(PumpSwitchMinOffTime, FirstTurnOnAlwaysAllowed) {
  GPIOPin pin;
  g_mock_millis = 0;
  PumpSwitch p = make_pump(pin);
  p.set_min_on_ms(0);
  p.set_min_off_ms(500);  // large off constraint
  // Never been on or off — first turn_on() must succeed immediately.
  p.turn_on();
  EXPECT_TRUE(p.is_running());
}

// ---------------------------------------------------------------------------
// Flow-rate bang-bang control — set_target_flow()
//
// When a target flow rate is set the pump modulates on/off in loop() to
// maintain the requested rate. The orchestrator uses this so that steam/brew
// simply declare a desired flow and the pump provides it.
// ---------------------------------------------------------------------------

TEST(PumpSwitchTargetFlow, TurnsOnWhenRateBelowTarget) {
  GPIOPin pin;
  MockFlowMeter fm;
  PumpSwitch p = make_pump(pin);
  p.set_flow_meter(&fm);

  fm.rate = 0.0f;          // below target
  p.set_target_flow(2.0f);
  p.loop();                // should start the pump
  EXPECT_TRUE(p.is_running());
  EXPECT_TRUE(pin.state_);
}

TEST(PumpSwitchTargetFlow, TurnsOffWhenRateAtTarget) {
  GPIOPin pin;
  MockFlowMeter fm;
  PumpSwitch p = make_pump(pin);
  p.set_flow_meter(&fm);

  fm.rate = 0.0f;
  p.set_target_flow(2.0f);
  p.loop();               // turns on (rate < target)
  EXPECT_TRUE(p.is_running());

  fm.rate = 2.0f;         // at target
  p.loop();               // should stop the pump
  EXPECT_FALSE(p.is_running());
  EXPECT_FALSE(pin.state_);
}

TEST(PumpSwitchTargetFlow, TurnsBackOnWhenRateDropsBelowTarget) {
  GPIOPin pin;
  MockFlowMeter fm;
  PumpSwitch p = make_pump(pin);
  p.set_flow_meter(&fm);

  p.set_target_flow(2.0f);
  fm.rate = 0.0f;
  p.loop();               // on
  fm.rate = 2.0f;
  p.loop();               // off at target
  fm.rate = 1.5f;         // drops below target
  p.loop();               // should turn back on
  EXPECT_TRUE(p.is_running());
}

TEST(PumpSwitchTargetFlow, RunsContinuouslyWithoutFlowMeter) {
  GPIOPin pin;
  PumpSwitch p = make_pump(pin);
  // No flow meter — get_rate() returns 0 (< target) so pump stays on
  p.set_target_flow(2.0f);
  for (int i = 0; i < 10; i++)
    p.loop();
  EXPECT_TRUE(p.is_running());
}

TEST(PumpSwitchTargetFlow, ClearTargetFlowDisablesBangBang) {
  GPIOPin pin;
  MockFlowMeter fm;
  PumpSwitch p = make_pump(pin);
  p.set_flow_meter(&fm);

  p.set_target_flow(2.0f);
  fm.rate = 0.0f;
  p.loop();               // turns on
  EXPECT_TRUE(p.is_running());

  p.set_target_flow(0.0f);  // disable flow control
  p.turn_off();             // external stop is now honoured
  EXPECT_FALSE(p.is_running());

  fm.rate = 0.0f;
  p.loop();               // should NOT restart (flow control cleared)
  EXPECT_FALSE(p.is_running());
}

// ---------------------------------------------------------------------------
// PumpNumber (dimmer) tests
// ---------------------------------------------------------------------------

TEST(PumpNumber, InitiallyOffAfterSetup) {
  GPIOPin pin;
  PumpNumber p;
  p.set_pin(&pin);
  p.setup();
  EXPECT_FALSE(p.is_running());
  EXPECT_FALSE(pin.state_);
  EXPECT_FLOAT_EQ(p.state, 0.0f);
}

TEST(PumpNumber, TurnOnSets100Percent) {
  GPIOPin pin;
  PumpNumber p;
  p.set_pin(&pin);
  p.setup();
  p.turn_on();
  EXPECT_TRUE(p.is_running());
  EXPECT_TRUE(pin.state_);
  EXPECT_FLOAT_EQ(p.state, 100.0f);
}

TEST(PumpNumber, TurnOffSetsZeroPercent) {
  GPIOPin pin;
  PumpNumber p;
  p.set_pin(&pin);
  p.setup();
  p.turn_on();
  p.turn_off();
  EXPECT_FALSE(p.is_running());
  EXPECT_FALSE(pin.state_);
  EXPECT_FLOAT_EQ(p.state, 0.0f);
}
