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

  MockPumpFixture(float nominal_flow = 4.0f, float puck_tau = 10.0f,
                  float pump_max_pressure = 15.0f, float puck_pressure = 9.0f) {
    pump.set_nominal_flow(nominal_flow);
    pump.set_puck_time_constant(puck_tau);
    pump.set_pump_max_pressure(pump_max_pressure);
    pump.set_puck_pressure(puck_pressure);
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
//
// At 9 bar puck (default) with 15 bar stall pressure, the new pump curve model
// reduces exactly to the old behaviour:
//   Q_max = Q_nom / (1 - 9/15) = Q_nom / 0.4 = 2.5 × Q_nom
//   Q_ss = Q_max × (1 - 9/15) = Q_nom          [steady state at 9 bar]
//   effective_τ = τ × (9/9) = τ                 [time constant unchanged at 9 bar]
//   Q(t) = Q_nom × (1 - exp(-t/τ))              [same exponential ramp as before]
// ---------------------------------------------------------------------------

TEST(MockPump, FlowStartsNearZero) {
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.turn_on();
  f.advance_time_ms(10);  // Very short time

  // Q(0.01s) = Q_nom × (1 - exp(-0.01/10)) ≈ 4 × 0.001 ≈ 0.004 mL/s
  EXPECT_NEAR(f.pump.get_flow_rate(), 0.004f, 0.01f);
}

TEST(MockPump, FlowRampsExponentially) {
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.turn_on();

  // Run for 1 second (100 × 10ms steps)
  for (int i = 0; i < 100; ++i) {
    f.advance_time_ms(10);
  }

  // At 9 bar (default), pump curve reduces to original exponential ramp:
  // Q(1s) = Q_nom × (1 - exp(-1/10)) ≈ 4 × 0.0952 ≈ 0.38 mL/s
  float expected = 4.0f * (1.0f - std::exp(-1.0f / 10.0f));
  EXPECT_NEAR(f.pump.get_flow_rate(), expected, 0.02f);
}

TEST(MockPump, FlowApproachesNominalAfterSeveralTau) {
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.turn_on();

  // Run for 50 seconds (5τ at 9 bar — 99.3% of steady state)
  for (int i = 0; i < 5000; ++i) {
    f.advance_time_ms(10);
  }

  // At 9 bar, pump curve reduces to Q_nom × (1 - exp(-5)) ≈ Q_nom
  EXPECT_NEAR(f.pump.get_flow_rate(), 4.0f, 0.1f);
}

TEST(MockPump, ZeroTimeConstantGivesImmediateFlow) {
  MockPumpFixture f(4.0f, 0.0f);  // τ = 0 → immediate full wetting
  f.pump.turn_on();
  f.advance_time_ms(10);

  // With τ = 0, effective_τ = 0 → wetted_fraction = 1 immediately
  // At 9 bar (default): Q = Q_ss = Q_nom = 4.0 mL/s
  EXPECT_FLOAT_EQ(f.pump.get_flow_rate(), 4.0f);
}

// ---------------------------------------------------------------------------
// Pump curve tests — verify physically-correct pressure-flow relationship
//
// Model: Q_ss = Q_max × (1 − P_puck / P_stall)
//        Q_max calibrated so Q_ss = Q_nom at P_puck = 9 bar
//        effective_τ = τ × (P_puck / 9)  [wetting scales with pressure]
// ---------------------------------------------------------------------------

TEST(MockPump, HardPuckReducesSteadyStateFlow) {
  // Q_max = nominal_flow / (1 - 9/P_stall) = 4 / (1 - 9/15) = 4 / 0.4 = 10 mL/s
  // At 12 bar puck: Q_ss = Q_max × (1 - 12/15) = 10 × 0.2 = 2.0 mL/s
  // This is 50% of nominal — physically correct (vs old model's 75%)
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.set_puck_pressure(12.0f);
  f.pump.set_pump_max_pressure(15.0f);
  f.pump.turn_on();

  // Run for 10× effective_τ (effective_τ = 10 × 12/9 ≈ 13.3s → 133s)
  for (int i = 0; i < 13300; ++i) {
    f.advance_time_ms(10);
  }

  // Should converge to ~2 mL/s, well below nominal (4 mL/s)
  EXPECT_NEAR(f.pump.get_flow_rate(), 2.0f, 0.15f);
}

TEST(MockPump, EasyPuckExceedsNominalFlow) {
  // Q_max = 4 / (1 - 9/15) = 10 mL/s
  // At 6 bar puck: Q_ss = Q_max × (1 - 6/15) = 10 × 0.6 = 6.0 mL/s
  // Easy puck → MORE flow than nominal (pump operates further up its curve)
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.set_puck_pressure(6.0f);
  f.pump.set_pump_max_pressure(15.0f);
  f.pump.turn_on();

  // effective_τ = 10 × 6/9 = 6.67s → run for 5× effective_τ ≈ 33s
  for (int i = 0; i < 3300; ++i) {
    f.advance_time_ms(10);
  }

  // Should converge to ~6 mL/s (significantly above nominal 4 mL/s)
  EXPECT_NEAR(f.pump.get_flow_rate(), 6.0f, 0.3f);
  EXPECT_GT(f.pump.get_flow_rate(), 4.0f);  // Strictly more than nominal
}

TEST(MockPump, HardPuckExtendsWettingPhase) {
  // Harder puck: effective_τ = τ × (P_puck / 9) is longer
  // At 9 bar: effective_τ = 10s → at 1s: Q ≈ Q_ss × (1 - exp(-0.1)) ≈ Q_ss × 0.095
  // At 12 bar: effective_τ = 13.3s → at 1s: Q ≈ Q_ss × (1 - exp(-0.075)) ≈ Q_ss × 0.072
  // Also Q_ss(12 bar) < Q_ss(9 bar), so flow at 1s is doubly lower for hard puck

  MockPumpFixture f_nominal(4.0f, 10.0f);
  f_nominal.pump.set_puck_pressure(9.0f);
  f_nominal.pump.set_pump_max_pressure(15.0f);

  MockPumpFixture f_hard(4.0f, 10.0f);
  f_hard.pump.set_puck_pressure(12.0f);
  f_hard.pump.set_pump_max_pressure(15.0f);

  f_nominal.pump.turn_on();
  f_hard.pump.turn_on();

  // Run for 1 second
  for (int i = 0; i < 100; ++i) {
    f_nominal.advance_time_ms(10);
    f_hard.advance_time_ms(10);
  }

  // Hard puck should have significantly less flow after 1s (longer wetting phase)
  EXPECT_LT(f_hard.pump.get_flow_rate(), f_nominal.pump.get_flow_rate());
}

TEST(MockPump, StallPressurePuckStopsFlow) {
  // Puck at pump stall pressure — no flow possible
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.set_puck_pressure(15.0f);
  f.pump.set_pump_max_pressure(15.0f);
  f.pump.turn_on();

  for (int i = 0; i < 1000; ++i) {
    f.advance_time_ms(10);
  }

  EXPECT_FLOAT_EQ(f.pump.get_flow_rate(), 0.0f);
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
