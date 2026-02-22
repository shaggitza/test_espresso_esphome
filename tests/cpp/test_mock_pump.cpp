#include <gtest/gtest.h>
#include <cmath>
#include "esphome/core/hal.h"
#include "espresso_machine_mock_pump/mock_pump.h"
#include "espresso_machine/interfaces.h"

using namespace esphome::espresso_machine_mock_pump;
using namespace esphome::espresso_machine;

extern uint32_t g_mock_millis;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

// Build and setup a MockPump with given parameters.
static MockPump make_pump(float nominal_flow = 4.0f, float puck_tau = 10.0f) {
  MockPump p;
  p.set_nominal_flow_ml_per_s(nominal_flow);
  p.set_puck_time_constant_s(puck_tau);
  g_mock_millis = 0;
  p.setup();
  return p;
}

// Advance simulated time by n steps of 50 ms, running loop() each step.
static void advance(MockPump &p, int steps) {
  for (int i = 0; i < steps; i++) {
    g_mock_millis += 50;
    p.loop();
  }
}

// ---------------------------------------------------------------------------
// Initial state
// ---------------------------------------------------------------------------

TEST(MockPump, InitiallyNotRunning) {
  MockPump p = make_pump();
  EXPECT_FALSE(p.is_running());
}

TEST(MockPump, InitialFlowRateIsZero) {
  MockPump p = make_pump();
  EXPECT_FLOAT_EQ(p.get_flow_rate(), 0.0f);
}

TEST(MockPump, InitialFlowTotalIsZero) {
  MockPump p = make_pump();
  EXPECT_FLOAT_EQ(p.get_flow_total(), 0.0f);
}

// ---------------------------------------------------------------------------
// Turn on / turn off
// ---------------------------------------------------------------------------

TEST(MockPump, TurnOnStartsRunning) {
  MockPump p = make_pump();
  p.turn_on();
  EXPECT_TRUE(p.is_running());
}

TEST(MockPump, TurnOffStopsRunning) {
  MockPump p = make_pump();
  p.turn_on();
  p.turn_off();
  EXPECT_FALSE(p.is_running());
}

TEST(MockPump, TurnOnIsIdempotent) {
  MockPump p = make_pump();
  p.turn_on();
  p.turn_on();
  EXPECT_TRUE(p.is_running());
}

TEST(MockPump, TurnOffIsIdempotent) {
  MockPump p = make_pump();
  p.turn_off();
  p.turn_off();
  EXPECT_FALSE(p.is_running());
}

TEST(MockPump, TurnOffZerosFlowRate) {
  MockPump p = make_pump();
  p.turn_on();
  advance(p, 100);  // some flow has built up
  p.turn_off();
  EXPECT_FLOAT_EQ(p.get_flow_rate(), 0.0f);
}

// ---------------------------------------------------------------------------
// Puck wetting flow model
// ---------------------------------------------------------------------------

TEST(MockPump, FlowRateStartsNearZero) {
  MockPump p = make_pump(4.0f, 10.0f);
  p.turn_on();
  // First loop step (50 ms): time_on = 0.05 s, Q = 4*(1-exp(-0.05/10)) ≈ 0.02 ml/s
  g_mock_millis += 50;
  p.loop();
  EXPECT_LT(p.get_flow_rate(), 0.1f);
}

TEST(MockPump, FlowRateIncreasesOverTime) {
  MockPump p = make_pump(4.0f, 10.0f);
  p.turn_on();

  advance(p, 10);   // 0.5 s
  float q_early = p.get_flow_rate();
  advance(p, 100);  // +5 s more
  float q_later = p.get_flow_rate();
  EXPECT_GT(q_later, q_early);
}

TEST(MockPump, FlowRateAtOneTauMatchesModel) {
  // After one time constant (tau = 10 s = 200 steps of 50 ms),
  // Q should be ≈ nominal × (1 − 1/e) = 4.0 × 0.6321 ≈ 2.528 ml/s.
  MockPump p = make_pump(4.0f, 10.0f);
  p.turn_on();
  advance(p, 200);  // 10 seconds
  float expected = 4.0f * (1.0f - expf(-1.0f));
  EXPECT_NEAR(p.get_flow_rate(), expected, 0.05f);
}

TEST(MockPump, FlowRateAtThreeTauIsNearNominal) {
  // After 3 × tau = 30 s, Q ≈ 0.95 × nominal = 3.80 ml/s.
  MockPump p = make_pump(4.0f, 10.0f);
  p.turn_on();
  advance(p, 600);  // 30 seconds
  EXPECT_NEAR(p.get_flow_rate(), 4.0f * (1.0f - expf(-3.0f)), 0.05f);
}

TEST(MockPump, FlowStopsAccumulatingWhenPumpOff) {
  MockPump p = make_pump(4.0f, 10.0f);
  p.turn_on();
  advance(p, 100);  // 5 s

  float vol_at_stop = p.get_flow_total();
  p.turn_off();
  advance(p, 100);  // 5 more seconds — pump off, no accumulation
  EXPECT_FLOAT_EQ(p.get_flow_total(), vol_at_stop);
}

TEST(MockPump, VolumeAccumulatesWhileRunning) {
  MockPump p = make_pump(4.0f, 10.0f);
  p.turn_on();
  advance(p, 100);  // 5 seconds
  EXPECT_GT(p.get_flow_total(), 0.0f);
}

TEST(MockPump, ShorterTauReachesNominalFaster) {
  // Small tau = fast puck saturation → higher volume in the same time.
  // Test each pump independently so the shared g_mock_millis doesn't interfere.
  MockPump fast = make_pump(4.0f, 2.0f);  // resets g_mock_millis = 0
  fast.turn_on();
  advance(fast, 100);  // 5 seconds
  float fast_volume = fast.get_flow_total();

  MockPump slow = make_pump(4.0f, 10.0f);  // resets g_mock_millis = 0
  slow.turn_on();
  advance(slow, 100);  // 5 seconds
  float slow_volume = slow.get_flow_total();

  EXPECT_GT(fast_volume, slow_volume);
}

TEST(MockPump, HigherNominalFlowGivesMoreVolume) {
  MockPump high = make_pump(8.0f, 10.0f);
  MockPump low = make_pump(2.0f, 10.0f);
  high.turn_on();
  low.turn_on();
  advance(high, 200);  // 10 seconds
  advance(low, 200);
  EXPECT_GT(high.get_flow_total(), low.get_flow_total());
}

// ---------------------------------------------------------------------------
// reset_flow
// ---------------------------------------------------------------------------

TEST(MockPump, ResetFlowClearsTotal) {
  MockPump p = make_pump();
  p.turn_on();
  advance(p, 100);
  p.reset_flow();
  EXPECT_FLOAT_EQ(p.get_flow_total(), 0.0f);
}

TEST(MockPump, ResetFlowClearsRate) {
  MockPump p = make_pump();
  p.turn_on();
  advance(p, 100);
  p.reset_flow();
  EXPECT_FLOAT_EQ(p.get_flow_rate(), 0.0f);
}

TEST(MockPump, ResetFlowResetsOnTime) {
  // After reset, the puck wetting starts fresh: flow rate returns to ≈ 0.
  MockPump p = make_pump(4.0f, 10.0f);
  p.turn_on();
  advance(p, 200);  // 10 s — puck half saturated
  EXPECT_GT(p.get_flow_rate(), 2.0f);

  p.reset_flow();
  // One loop step after reset — flow should be back near zero
  advance(p, 1);
  EXPECT_LT(p.get_flow_rate(), 0.1f);
}

TEST(MockPump, ResetFlowWhileOffIsNoOp) {
  MockPump p = make_pump();
  EXPECT_NO_FATAL_FAILURE(p.reset_flow());
  EXPECT_FLOAT_EQ(p.get_flow_total(), 0.0f);
}

// ---------------------------------------------------------------------------
// IPump interface — used by the orchestrator
// ---------------------------------------------------------------------------

TEST(MockPump, IPumpTurnOnOffIsRunning) {
  MockPump p = make_pump();
  IPump *pump = &p;
  pump->turn_on();
  EXPECT_TRUE(pump->is_running());
  pump->turn_off();
  EXPECT_FALSE(pump->is_running());
}

TEST(MockPump, IPumpGetFlowRateAndTotalWhileRunning) {
  MockPump p = make_pump();
  IPump *pump = &p;
  pump->turn_on();
  advance(p, 200);  // 10 s
  EXPECT_GT(pump->get_flow_rate(), 0.0f);
  EXPECT_GT(pump->get_flow_total(), 0.0f);
}

TEST(MockPump, IPumpResetFlowResetsVolume) {
  MockPump p = make_pump();
  IPump *pump = &p;
  pump->turn_on();
  advance(p, 200);
  pump->reset_flow();
  EXPECT_FLOAT_EQ(pump->get_flow_total(), 0.0f);
}

// ---------------------------------------------------------------------------
// HA runtime parameter adjustments
// ---------------------------------------------------------------------------

TEST(MockPump, RuntimeNominalFlowAffectsFlowRate) {
  // Raise nominal flow to 8 ml/s — after 2 tau (10 s), flow should reflect new value.
  MockPump p = make_pump(4.0f, 5.0f);
  p.set_nominal_flow_runtime(8.0f);  // simulates HA slider
  p.turn_on();
  advance(p, 200);  // 10 s = 2 tau (tau=5s)
  float expected_rate = 8.0f * (1.0f - expf(-2.0f));  // ≈ 7.29 ml/s
  EXPECT_NEAR(p.get_flow_rate(), expected_rate, 0.1f);
}

TEST(MockPump, RuntimePuckTauAffectsFlowRate) {
  // Reduce puck tau: puck saturates much faster.
  // Test each pump independently so the shared g_mock_millis doesn't interfere.
  MockPump fast = make_pump(4.0f, 10.0f);  // resets g_mock_millis = 0
  fast.set_puck_tau_runtime(2.0f);  // simulates HA slider
  fast.turn_on();
  advance(fast, 100);  // 5 s (= 2.5 tau for fast pump)
  float fast_volume = fast.get_flow_total();

  MockPump slow = make_pump(4.0f, 10.0f);  // resets g_mock_millis = 0
  slow.turn_on();
  advance(slow, 100);  // 5 s (= 0.5 tau for slow pump)
  float slow_volume = slow.get_flow_total();

  EXPECT_GT(fast_volume, slow_volume);
}

TEST(MockPump, RuntimeNominalFlowIgnoresZero) {
  MockPump p = make_pump(4.0f, 10.0f);
  p.set_nominal_flow_runtime(0.0f);  // invalid — must be ignored
  p.turn_on();
  advance(p, 200);  // internal nominal still 4 ml/s
  EXPECT_GT(p.get_flow_rate(), 0.0f);
}

TEST(MockPump, RuntimePuckTauIgnoresZero) {
  MockPump p = make_pump(4.0f, 10.0f);
  p.set_puck_tau_runtime(0.0f);  // invalid — must be ignored
  p.turn_on();
  EXPECT_NO_FATAL_FAILURE(advance(p, 10));  // must not divide by zero
}

TEST(MockPump, ParamNumberPublishesStateOnControl) {
  // Verify state storage via publish_state (control() is tested indirectly).
  MockPumpParamNumber num;
  num.publish_state(42.0f);
  EXPECT_FLOAT_EQ(num.state, 42.0f);
}

// ---------------------------------------------------------------------------
// Loop skips short intervals
// ---------------------------------------------------------------------------

TEST(MockPump, LoopSkipsUpdateIfIntervalTooShort) {
  MockPump p = make_pump();
  p.turn_on();
  g_mock_millis = 0;
  // First loop at 40 ms — less than 50 ms threshold; must skip
  g_mock_millis = 40;
  p.loop();
  EXPECT_FLOAT_EQ(p.get_flow_rate(), 0.0f);
  EXPECT_FLOAT_EQ(p.get_flow_total(), 0.0f);
}
