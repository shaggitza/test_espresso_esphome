#include <gtest/gtest.h>
#include "espresso_machine_valve/valve.h"

using namespace esphome::espresso_machine_valve;

// Helper: create and set up a valve with a given normally_open setting
static Valve make_valve(GPIOPin &pin, bool normally_open = false) {
  Valve v;
  v.set_pin(&pin);
  v.set_normally_open(normally_open);
  v.setup();
  return v;
}

// ---- Normally-Closed (normally_open = false) --------------------------------

TEST(Valve, InitiallyClosedAfterSetup) {
  GPIOPin pin;
  Valve v = make_valve(pin, false);
  EXPECT_FALSE(v.is_open());
  EXPECT_FALSE(pin.state_);  // GPIO LOW == coil de-energised == closed
}

TEST(Valve, OpenSetsHighPin) {
  GPIOPin pin;
  Valve v = make_valve(pin, false);
  v.open();
  EXPECT_TRUE(v.is_open());
  EXPECT_TRUE(pin.state_);
}

TEST(Valve, CloseSetsLowPin) {
  GPIOPin pin;
  Valve v = make_valve(pin, false);
  v.open();
  v.close();
  EXPECT_FALSE(v.is_open());
  EXPECT_FALSE(pin.state_);
}

TEST(Valve, OpenIsIdempotent) {
  GPIOPin pin;
  Valve v = make_valve(pin, false);
  v.open();
  v.open();  // second call must not change state or flip pin
  EXPECT_TRUE(v.is_open());
  EXPECT_TRUE(pin.state_);
}

TEST(Valve, CloseIsIdempotent) {
  GPIOPin pin;
  Valve v = make_valve(pin, false);
  v.close();
  v.close();
  EXPECT_FALSE(v.is_open());
  EXPECT_FALSE(pin.state_);
}

// ---- Normally-Open (normally_open = true) -----------------------------------

TEST(Valve, NormallyOpenInitiallyPhysicallyClosed) {
  GPIOPin pin;
  Valve v = make_valve(pin, true);
  // Valve is logically closed: pin is HIGH to hold the coil energised and
  // keep the normally-open orifice closed.
  EXPECT_FALSE(v.is_open());
  EXPECT_TRUE(pin.state_);
}

TEST(Valve, NormallyOpenOpenSetsLowPin) {
  GPIOPin pin;
  Valve v = make_valve(pin, true);
  v.open();
  EXPECT_TRUE(v.is_open());
  EXPECT_FALSE(pin.state_);  // de-energised → orifice opens
}

TEST(Valve, NormallyOpenCloseSetsHighPin) {
  GPIOPin pin;
  Valve v = make_valve(pin, true);
  v.open();
  v.close();
  EXPECT_FALSE(v.is_open());
  EXPECT_TRUE(pin.state_);  // energised → orifice closes
}
