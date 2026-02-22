#include <gtest/gtest.h>
#include "esphome/core/hal.h"
#include "espresso_machine_pump/pump.h"

using namespace esphome::espresso_machine_pump;

extern uint32_t g_mock_millis;

static PumpSwitch make_pump(GPIOPin &pin) {
  PumpSwitch p;
  p.set_pin(&pin);
  p.setup();
  return p;
}

// ---------------------------------------------------------------------------
// PumpSwitch (relay) tests
// ---------------------------------------------------------------------------

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

TEST(PumpSwitch, RunTimedStopsPumpAfterDuration) {
  GPIOPin pin;
  g_mock_millis = 0;
  PumpSwitch p = make_pump(pin);
  p.run(500);
  EXPECT_TRUE(p.is_running());
  // Still running 1 ms before deadline
  g_mock_millis = 499;
  p.loop();
  EXPECT_TRUE(p.is_running());
  // At deadline loop() should stop the pump
  g_mock_millis = 500;
  p.loop();
  EXPECT_FALSE(p.is_running());
  EXPECT_FALSE(pin.state_);
}

TEST(PumpSwitch, RunTimedCanBeStoppedEarly) {
  GPIOPin pin;
  g_mock_millis = 0;
  PumpSwitch p = make_pump(pin);
  p.run(5000);
  EXPECT_TRUE(p.is_running());
  p.turn_off();
  EXPECT_FALSE(p.is_running());
  // loop() should not restart it
  g_mock_millis = 5000;
  p.loop();
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
