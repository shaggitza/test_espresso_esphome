#include <gtest/gtest.h>
#include "esphome/core/hal.h"
#include "espresso_machine_grinder/grinder.h"

using namespace esphome::espresso_machine_grinder;

extern uint32_t g_mock_millis;

static Grinder make_grinder(GPIOPin &pin, uint32_t default_ms = 7000,
                             GrinderType type = GrinderType::RELAY) {
  Grinder g;
  g.set_pin(&pin);
  g.set_grinder_type(type);
  g.set_default_grind_time(default_ms);
  g.setup();
  return g;
}

// ---------------------------------------------------------------------------
// Basic grind behaviour
// ---------------------------------------------------------------------------

TEST(Grinder, InitiallyNotGrinding) {
  GPIOPin pin;
  Grinder g = make_grinder(pin);
  EXPECT_FALSE(g.is_grinding());
  EXPECT_FALSE(pin.state_);
}

TEST(Grinder, GrindActivatesPin) {
  GPIOPin pin;
  Grinder g = make_grinder(pin, 7000);
  g_mock_millis = 0;
  g.grind();
  EXPECT_TRUE(g.is_grinding());
  EXPECT_TRUE(pin.state_);
}

TEST(Grinder, GrindUsesDefaultDurationWhenZero) {
  GPIOPin pin;
  g_mock_millis = 0;
  Grinder g = make_grinder(pin, 5000);
  g.grind(0);
  EXPECT_TRUE(g.is_grinding());
  // Still grinding 1 ms before the deadline
  g_mock_millis = 4999;
  g.loop();
  EXPECT_TRUE(g.is_grinding());
  // At the deadline, loop() should stop the grind
  g_mock_millis = 5000;
  g.loop();
  EXPECT_FALSE(g.is_grinding());
  EXPECT_FALSE(pin.state_);
}

TEST(Grinder, GrindUsesExplicitDuration) {
  GPIOPin pin;
  g_mock_millis = 1000;
  Grinder g = make_grinder(pin, 7000);
  g.grind(3000);
  EXPECT_TRUE(g.is_grinding());
  g_mock_millis = 3999;
  g.loop();
  EXPECT_TRUE(g.is_grinding());
  g_mock_millis = 4000;
  g.loop();
  EXPECT_FALSE(g.is_grinding());
}

TEST(Grinder, StopImmediatelyHalts) {
  GPIOPin pin;
  g_mock_millis = 0;
  Grinder g = make_grinder(pin, 7000);
  g.grind();
  EXPECT_TRUE(g.is_grinding());
  g.stop();
  EXPECT_FALSE(g.is_grinding());
  EXPECT_FALSE(pin.state_);
}

TEST(Grinder, StopIsIdempotent) {
  GPIOPin pin;
  g_mock_millis = 0;
  Grinder g = make_grinder(pin, 7000);
  g.stop();
  g.stop();
  EXPECT_FALSE(g.is_grinding());
}

TEST(Grinder, MillisRolloverHandledCorrectly) {
  // start = 0xFFFFFF00, duration = 300 ms.
  // 0xFFFFFF00 + 300 = 4294967340 > 0xFFFFFFFF → wraps to 44 = 0x2C.
  // So grind_end_ms_ = 0x2C after rollover.
  GPIOPin pin;
  g_mock_millis = 0xFFFFFF00U;
  Grinder g = make_grinder(pin, 300);
  g.grind(300);
  EXPECT_TRUE(g.is_grinding());
  // Still before deadline — millis has wrapped but not reached 0x2C yet.
  g_mock_millis = 0x0000002BU;
  g.loop();
  EXPECT_TRUE(g.is_grinding());
  // At deadline: (int32_t)(0x2C - 0x2C) == 0 ≥ 0 → stop.
  g_mock_millis = 0x0000002CU;
  g.loop();
  EXPECT_FALSE(g.is_grinding());
  EXPECT_FALSE(pin.state_);
}

TEST(Grinder, TypeNoneDoesNotActivatePin) {
  GPIOPin pin;
  g_mock_millis = 0;
  Grinder g = make_grinder(pin, 7000, GrinderType::NONE);
  g.grind();
  EXPECT_FALSE(g.is_grinding());
  EXPECT_FALSE(pin.state_);
}

// ---------------------------------------------------------------------------
// Button entity — press() triggers a grind
// ---------------------------------------------------------------------------

TEST(Grinder, PressTriggersGrind) {
  GPIOPin pin;
  g_mock_millis = 0;
  Grinder g = make_grinder(pin, 7000);
  g.press();  // button entity API
  EXPECT_TRUE(g.is_grinding());
  EXPECT_TRUE(pin.state_);
}

// ---------------------------------------------------------------------------
// GrinderTimeNumber — adjustable grind time via HA number entity
// ---------------------------------------------------------------------------

TEST(Grinder, GrinderTimeNumberPublishesInitialState) {
  GPIOPin pin;
  Grinder g = make_grinder(pin, 7000);

  GrinderTimeNumber num(&g);
  g.set_grind_time_number(&num);
  g.setup();  // should call num.publish_state(7000)

  EXPECT_FLOAT_EQ(num.state, 7000.0f);
}

TEST(Grinder, GrinderTimeNumberUpdatesGrindDuration) {
  GPIOPin pin;
  g_mock_millis = 0;
  Grinder g = make_grinder(pin, 7000);

  // Update the default time directly (simulates what GrinderTimeNumber::control() does)
  g.set_default_grind_time(5000);

  g.grind(0);  // uses default
  g_mock_millis = 4999;
  g.loop();
  EXPECT_TRUE(g.is_grinding());
  g_mock_millis = 5000;
  g.loop();
  EXPECT_FALSE(g.is_grinding());
}
