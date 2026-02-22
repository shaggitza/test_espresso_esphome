#include <gtest/gtest.h>
#include "espresso_machine_valve/valve.h"

using namespace esphome::espresso_machine_valve;

// Helper: create and set up a valve with a given normally_open setting.
// NOTE: relies on NRVO so that &v inside make_valve == &v at the call site.
static Valve make_valve(GPIOPin &pin, bool normally_open = false) {
  Valve v;
  v.set_pin(&pin);
  v.set_normally_open(normally_open);
  v.setup();
  return v;
}

// ---- Normally-Closed (normally_open = false) --------------------------------

TEST(Valve, InitiallyClosedAfterSetup) {
  Valve::reset_registry();
  GPIOPin pin;
  Valve v = make_valve(pin, false);
  EXPECT_FALSE(v.is_open());
  EXPECT_FALSE(pin.state_);  // GPIO LOW == coil de-energised == closed
  EXPECT_FALSE(v.state);     // switch entity reports closed to HA
}

TEST(Valve, OpenSetsHighPin) {
  Valve::reset_registry();
  GPIOPin pin;
  Valve v = make_valve(pin, false);
  v.open();
  EXPECT_TRUE(v.is_open());
  EXPECT_TRUE(pin.state_);
}

TEST(Valve, CloseSetsLowPin) {
  Valve::reset_registry();
  GPIOPin pin;
  Valve v = make_valve(pin, false);
  v.open();
  v.close();
  EXPECT_FALSE(v.is_open());
  EXPECT_FALSE(pin.state_);
}

TEST(Valve, OpenIsIdempotent) {
  Valve::reset_registry();
  GPIOPin pin;
  Valve v = make_valve(pin, false);
  v.open();
  v.open();  // second call must not change state or flip pin
  EXPECT_TRUE(v.is_open());
  EXPECT_TRUE(pin.state_);
}

TEST(Valve, CloseIsIdempotent) {
  Valve::reset_registry();
  GPIOPin pin;
  Valve v = make_valve(pin, false);
  v.close();
  v.close();
  EXPECT_FALSE(v.is_open());
  EXPECT_FALSE(pin.state_);
}

// ---- Normally-Open (normally_open = true) -----------------------------------

TEST(Valve, NormallyOpenInitiallyPhysicallyClosed) {
  Valve::reset_registry();
  GPIOPin pin;
  Valve v = make_valve(pin, true);
  // Valve is logically closed: pin is HIGH to hold the coil energised and
  // keep the normally-open orifice closed.
  EXPECT_FALSE(v.is_open());
  EXPECT_TRUE(pin.state_);
}

TEST(Valve, NormallyOpenOpenSetsLowPin) {
  Valve::reset_registry();
  GPIOPin pin;
  Valve v = make_valve(pin, true);
  v.open();
  EXPECT_TRUE(v.is_open());
  EXPECT_FALSE(pin.state_);  // de-energised → orifice opens
}

TEST(Valve, NormallyOpenCloseSetsHighPin) {
  Valve::reset_registry();
  GPIOPin pin;
  Valve v = make_valve(pin, true);
  v.open();
  v.close();
  EXPECT_FALSE(v.is_open());
  EXPECT_TRUE(pin.state_);  // energised → orifice closes
}

// ---- Switch entity (turn_on / turn_off via switch_ public API) -------------

TEST(Valve, TurnOnOpensValve) {
  Valve::reset_registry();
  GPIOPin pin;
  Valve v = make_valve(pin, false);
  v.turn_on();
  EXPECT_TRUE(v.is_open());
  EXPECT_TRUE(pin.state_);
  EXPECT_TRUE(v.state);  // HA entity state reflects open
}

TEST(Valve, TurnOffClosesValve) {
  Valve::reset_registry();
  GPIOPin pin;
  Valve v = make_valve(pin, false);
  v.turn_on();
  v.turn_off();
  EXPECT_FALSE(v.is_open());
  EXPECT_FALSE(pin.state_);
  EXPECT_FALSE(v.state);
}

// ---- Interlock (platform-level single-open invariant) ----------------------

class ValveInterlockTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Valve::reset_registry();
    v1_ = new Valve();
    v1_->set_pin(&pin1_);
    v1_->set_normally_open(false);
    v1_->setup();

    v2_ = new Valve();
    v2_->set_pin(&pin2_);
    v2_->set_normally_open(false);
    v2_->setup();
  }

  void TearDown() override {
    delete v1_;
    delete v2_;
    Valve::reset_registry();
  }

  GPIOPin pin1_, pin2_;
  Valve *v1_{nullptr}, *v2_{nullptr};
};

TEST_F(ValveInterlockTest, OpeningOneClosesOthers) {
  v1_->open();
  EXPECT_TRUE(v1_->is_open());
  EXPECT_TRUE(pin1_.state_);

  // Opening v2 must close v1 via the interlock.
  v2_->open();
  EXPECT_TRUE(v2_->is_open());
  EXPECT_TRUE(pin2_.state_);
  EXPECT_FALSE(v1_->is_open());
  EXPECT_FALSE(pin1_.state_);
}

TEST_F(ValveInterlockTest, InterlockUpdatesHAStateOfClosedValve) {
  v1_->open();
  v2_->open();  // triggers interlock on v1

  // The HA entity state of v1 must reflect the forced close.
  EXPECT_FALSE(v1_->state);
  EXPECT_TRUE(v2_->state);
}

TEST_F(ValveInterlockTest, TurnOnEnforcesInterlock) {
  v1_->open();
  v2_->turn_on();
  EXPECT_TRUE(v2_->is_open());
  EXPECT_FALSE(v1_->is_open());
  EXPECT_FALSE(pin1_.state_);
}

TEST_F(ValveInterlockTest, OnlyOneValveOpenAtATime) {
  v1_->open();
  v2_->open();
  int open_count = (v1_->is_open() ? 1 : 0) + (v2_->is_open() ? 1 : 0);
  EXPECT_EQ(open_count, 1);
}
