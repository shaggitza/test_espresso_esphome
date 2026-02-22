#include <gtest/gtest.h>
#include "espresso_machine_pump/pump.h"

using namespace esphome::espresso_machine_pump;

static Pump make_pump(GPIOPin &pin, PumpType type = PumpType::RELAY) {
  Pump p;
  p.set_pin(&pin);
  p.set_pump_type(type);
  p.setup();
  return p;
}

TEST(Pump, InitiallyOffAfterSetup) {
  GPIOPin pin;
  Pump p = make_pump(pin);
  EXPECT_FALSE(p.is_running());
  EXPECT_FALSE(pin.state_);
}

TEST(Pump, TurnOnSetsHighPin) {
  GPIOPin pin;
  Pump p = make_pump(pin);
  p.turn_on();
  EXPECT_TRUE(p.is_running());
  EXPECT_TRUE(pin.state_);
}

TEST(Pump, TurnOffSetsLowPin) {
  GPIOPin pin;
  Pump p = make_pump(pin);
  p.turn_on();
  p.turn_off();
  EXPECT_FALSE(p.is_running());
  EXPECT_FALSE(pin.state_);
}

TEST(Pump, TurnOnIsIdempotent) {
  GPIOPin pin;
  Pump p = make_pump(pin);
  p.turn_on();
  p.turn_on();
  EXPECT_TRUE(p.is_running());
  EXPECT_TRUE(pin.state_);
}

TEST(Pump, TurnOffIsIdempotent) {
  GPIOPin pin;
  Pump p = make_pump(pin);
  p.turn_off();
  p.turn_off();
  EXPECT_FALSE(p.is_running());
  EXPECT_FALSE(pin.state_);
}

TEST(Pump, DimmerTypeSetupDoesNotCrash) {
  GPIOPin pin;
  Pump p = make_pump(pin, PumpType::DIMMER);
  p.turn_on();
  EXPECT_TRUE(p.is_running());
  p.turn_off();
  EXPECT_FALSE(p.is_running());
}
