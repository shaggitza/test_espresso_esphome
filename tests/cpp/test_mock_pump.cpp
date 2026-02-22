#include <gtest/gtest.h>
#include <cmath>
#include "esphome/core/hal.h"
#include "espresso_machine_mock_pump/mock_pump.h"

using namespace esphome::espresso_machine_mock_pump;
using esphome::sensor::Sensor;

extern uint32_t g_mock_millis;

// ---------------------------------------------------------------------------
// Helper to create a configured MockPump with sensors attached
// ---------------------------------------------------------------------------
struct MockPumpFixture {
  MockPump pump;
  Sensor rate_sensor;
  Sensor total_sensor;

  MockPumpFixture(float nominal_flow = 4.0f, float puck_tau = 10.0f) {
    pump.set_nominal_flow(nominal_flow);
    pump.set_puck_time_constant(puck_tau);
    pump.set_rate_sensor(&rate_sensor);
    pump.set_total_sensor(&total_sensor);

    g_mock_millis = 0;
    pump.setup();
  }

  void advance_time_ms(uint32_t dt_ms) {
    g_mock_millis += dt_ms;
    pump.loop();
  }
};

// ---------------------------------------------------------------------------
// Basic initialization tests
// ---------------------------------------------------------------------------

TEST(MockPump, InitialStateIsOff) {
  MockPumpFixture f;
  EXPECT_FALSE(f.pump.is_running());
  EXPECT_FLOAT_EQ(f.pump.get_flow_rate(), 0.0f);
  EXPECT_FLOAT_EQ(f.pump.get_flow_total(), 0.0f);
}

TEST(MockPump, ConfiguredParametersCorrect) {
  MockPumpFixture f(5.0f, 15.0f);
  EXPECT_FLOAT_EQ(f.pump.get_nominal_flow(), 5.0f);
  EXPECT_FLOAT_EQ(f.pump.get_puck_time_constant(), 15.0f);
}

// ---------------------------------------------------------------------------
// Pump on/off behavior
// ---------------------------------------------------------------------------

TEST(MockPump, TurnOnSetsRunning) {
  MockPumpFixture f;
  f.pump.turn_on();
  EXPECT_TRUE(f.pump.is_running());
}

TEST(MockPump, TurnOffStopsRunning) {
  MockPumpFixture f;
  f.pump.turn_on();
  f.pump.turn_off();
  EXPECT_FALSE(f.pump.is_running());
}

// ---------------------------------------------------------------------------
// Puck wetting flow model tests
// Q(t) = Q_nom × (1 − exp(−t/τ))
// ---------------------------------------------------------------------------

TEST(MockPump, FlowStartsNearZero) {
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.turn_on();
  f.advance_time_ms(10);  // Very short time

  // Q(0.01s) = 4 × (1 - exp(-0.01/10)) ≈ 4 × 0.001 ≈ 0.004 mL/s
  EXPECT_NEAR(f.pump.get_flow_rate(), 0.004f, 0.01f);
}

TEST(MockPump, FlowRampsExponentially) {
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.turn_on();

  // Run for 1 second (100 × 10ms steps)
  for (int i = 0; i < 100; ++i) {
    f.advance_time_ms(10);
  }

  // Q(1s) = 4 × (1 - exp(-1/10)) = 4 × (1 - exp(-0.1)) ≈ 4 × 0.0952 ≈ 0.38 mL/s
  float expected = 4.0f * (1.0f - std::exp(-1.0f / 10.0f));
  EXPECT_NEAR(f.pump.get_flow_rate(), expected, 0.02f);
}

TEST(MockPump, FlowApproachesNominalAfterSeveralTau) {
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.turn_on();

  // Run for 50 seconds (5τ — 99.3% of nominal)
  for (int i = 0; i < 5000; ++i) {
    f.advance_time_ms(10);
  }

  // Q(50s) = 4 × (1 - exp(-50/10)) = 4 × (1 - exp(-5)) ≈ 4 × 0.9933 ≈ 3.97 mL/s
  EXPECT_NEAR(f.pump.get_flow_rate(), 4.0f, 0.1f);
}

TEST(MockPump, ZeroTimeConstantGivesImmediateFlow) {
  MockPumpFixture f(4.0f, 0.0f);  // τ = 0 → immediate full flow
  f.pump.turn_on();
  f.advance_time_ms(10);

  // With τ = 0, flow should immediately be at nominal
  EXPECT_FLOAT_EQ(f.pump.get_flow_rate(), 4.0f);
}

// ---------------------------------------------------------------------------
// Volume accumulation tests
// ---------------------------------------------------------------------------

TEST(MockPump, VolumeAccumulatesWhileRunning) {
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.turn_on();

  // Run for 10 seconds
  for (int i = 0; i < 1000; ++i) {
    f.advance_time_ms(10);
  }

  // Total volume = ∫₀^10 Q_nom × (1 - exp(-t/τ)) dt
  // = Q_nom × [t + τ × exp(-t/τ)]₀^10
  // = Q_nom × [(10 + 10×exp(-1)) - (0 + 10×1)]
  // = 4 × [10 + 10×0.368 - 10] = 4 × 3.68 = 14.72 mL
  // (approximation due to discrete integration)
  EXPECT_GT(f.pump.get_flow_total(), 10.0f);
  EXPECT_LT(f.pump.get_flow_total(), 20.0f);
}

TEST(MockPump, VolumeStopsAccumulatingWhenOff) {
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.turn_on();

  // Run for 5 seconds
  for (int i = 0; i < 500; ++i) {
    f.advance_time_ms(10);
  }

  f.pump.turn_off();
  float volume_at_stop = f.pump.get_flow_total();

  // Run another 5 seconds with pump off
  for (int i = 0; i < 500; ++i) {
    f.advance_time_ms(10);
  }

  // Volume should not have increased significantly (small decay artifact OK)
  EXPECT_NEAR(f.pump.get_flow_total(), volume_at_stop, 0.1f);
}

// ---------------------------------------------------------------------------
// Reset flow tests
// ---------------------------------------------------------------------------

TEST(MockPump, ResetClearsFlowCounters) {
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.turn_on();

  // Run for 5 seconds to accumulate some volume
  for (int i = 0; i < 500; ++i) {
    f.advance_time_ms(10);
  }

  EXPECT_GT(f.pump.get_flow_total(), 0.0f);

  f.pump.reset_flow();

  EXPECT_FLOAT_EQ(f.pump.get_flow_total(), 0.0f);
  EXPECT_FLOAT_EQ(f.pump.get_flow_rate(), 0.0f);
}

TEST(MockPump, ResetDoesNotStopPump) {
  MockPumpFixture f;
  f.pump.turn_on();
  f.pump.reset_flow();
  EXPECT_TRUE(f.pump.is_running());
}

// ---------------------------------------------------------------------------
// Runtime parameter tuning
// ---------------------------------------------------------------------------

TEST(MockPump, RuntimeParameterUpdates) {
  MockPumpFixture f;

  f.pump.update_nominal_flow(6.0f);
  EXPECT_FLOAT_EQ(f.pump.get_nominal_flow(), 6.0f);

  f.pump.update_puck_time_constant(20.0f);
  EXPECT_FLOAT_EQ(f.pump.get_puck_time_constant(), 20.0f);
}

TEST(MockPump, ChangingNominalFlowAffectsFutureRate) {
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.turn_on();

  // Run briefly
  for (int i = 0; i < 100; ++i) {
    f.advance_time_ms(10);
  }

  float rate_before = f.pump.get_flow_rate();

  // Double the nominal flow
  f.pump.update_nominal_flow(8.0f);

  // Run more
  for (int i = 0; i < 100; ++i) {
    f.advance_time_ms(10);
  }

  // Rate should be higher now (approaching new nominal)
  EXPECT_GT(f.pump.get_flow_rate(), rate_before);
}

// ---------------------------------------------------------------------------
// Pump restart behavior
// ---------------------------------------------------------------------------

TEST(MockPump, RestartResetsRunTimeButNotVolume) {
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.turn_on();

  // Run for 5 seconds — accumulate volume and build flow rate
  for (int i = 0; i < 500; ++i) {
    f.advance_time_ms(10);
  }

  float volume_before = f.pump.get_flow_total();
  float rate_before = f.pump.get_flow_rate();

  // Stop then restart
  f.pump.turn_off();
  f.advance_time_ms(100);  // Let flow decay
  f.pump.turn_on();

  // After restart, run time is reset so flow starts ramping from near zero again
  f.advance_time_ms(10);

  // Rate should be low (fresh start on puck model)
  EXPECT_LT(f.pump.get_flow_rate(), rate_before);
  // But volume should still have the accumulated total (plus small new amount)
  EXPECT_GT(f.pump.get_flow_total(), volume_before - 0.1f);
}
