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

TEST(MockPump, VolumeStopsAccumulatingWhenOffAndNoPressureModel) {
  // Verify that with internal_volume = 0 (pressure model disabled), volume
  // stops accumulating as soon as the pump is off (legacy behaviour).
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.set_internal_volume(0.0f);  // Disable pressure-decay model
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
// Pressure buildup / residual-flow tests
//
// When internal_volume_ml > 0 the mock simulates the trapped pressure in
// tubing/piping continuing to drive flow after the pump stops.
// τ_decay = internal_volume_ml / nominal_flow
// ---------------------------------------------------------------------------

TEST(MockPump, ResidualFlowDecreasesGraduallyWithInternalVolume) {
  // With internal_volume_ml = 20, τ = 20/4 = 5 s.
  // After pump stops at steady state, flow should still be measurable
  // at 1 τ (i.e. 37% of steady-state) rather than snapping to zero.
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.set_internal_volume(20.0f);
  f.pump.turn_on();

  // Run to steady state (5τ of wetting = 50 s)
  for (int i = 0; i < 5000; ++i) {
    f.advance_time_ms(10);
  }
  float steady_flow = f.pump.get_flow_rate();
  ASSERT_NEAR(steady_flow, 4.0f, 0.1f);  // Sanity-check steady state

  // Stop pump
  f.pump.turn_off();

  // Immediately after stop, flow should still be positive (not zero)
  f.advance_time_ms(10);
  EXPECT_GT(f.pump.get_flow_rate(), 0.0f);

  // After 1 τ (5 s), flow should be significantly above zero (≥ 30% of steady)
  for (int i = 0; i < 499; ++i) {  // 499 × 10ms = 4.99 s (already did 1 step)
    f.advance_time_ms(10);
  }
  EXPECT_GT(f.pump.get_flow_rate(), steady_flow * 0.25f);
}

TEST(MockPump, ResidualFlowEventuallyReachesZero) {
  // After many decay time constants, flow must be essentially zero.
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.set_internal_volume(20.0f);
  f.pump.turn_on();

  // Run to steady state
  for (int i = 0; i < 5000; ++i) {
    f.advance_time_ms(10);
  }

  f.pump.turn_off();

  // Run for 10 τ (= 50 s at τ = 5 s): expect flow < 1% of steady state
  for (int i = 0; i < 5000; ++i) {
    f.advance_time_ms(10);
  }

  EXPECT_NEAR(f.pump.get_flow_rate(), 0.0f, 0.05f);
}

TEST(MockPump, LargerInternalVolumeSlowsDecay) {
  // Larger internal_volume → longer τ_decay → higher flow after equal elapsed time.
  MockPumpFixture f_small(4.0f, 10.0f);
  f_small.pump.set_internal_volume(10.0f);  // τ = 2.5 s

  MockPumpFixture f_large(4.0f, 10.0f);
  f_large.pump.set_internal_volume(40.0f);  // τ = 10 s

  f_small.pump.turn_on();
  f_large.pump.turn_on();

  // Run both to steady state
  for (int i = 0; i < 5000; ++i) {
    f_small.advance_time_ms(10);
    f_large.advance_time_ms(10);
  }

  f_small.pump.turn_off();
  f_large.pump.turn_off();

  // Advance 5 s: small volume should have decayed much more
  for (int i = 0; i < 500; ++i) {
    f_small.advance_time_ms(10);
    f_large.advance_time_ms(10);
  }

  EXPECT_LT(f_small.pump.get_flow_rate(), f_large.pump.get_flow_rate());
}

TEST(MockPump, ZeroInternalVolumeGivesQuickDecay) {
  // Disabling pressure model (internal_volume = 0) gives the legacy quick decay.
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.set_internal_volume(0.0f);
  f.pump.turn_on();

  // Run to steady state
  for (int i = 0; i < 5000; ++i) {
    f.advance_time_ms(10);
  }

  f.pump.turn_off();

  // After 2 s (200 × 10ms steps) with no internal volume, flow should be < 1% of steady.
  // Each step multiplies flow by 0.9, so after 200 steps: 0.9^200 ≈ 7e-10 ≈ 0.
  for (int i = 0; i < 200; ++i) {
    f.advance_time_ms(10);
  }

  EXPECT_NEAR(f.pump.get_flow_rate(), 0.0f, 0.01f);
}

TEST(MockPump, ResidualFlowAccumulatesVolume) {
  // Volume should keep accumulating after pump stops while pressure decays.
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.set_internal_volume(20.0f);
  f.pump.turn_on();

  // Run to steady state
  for (int i = 0; i < 5000; ++i) {
    f.advance_time_ms(10);
  }

  f.pump.turn_off();
  float volume_at_stop = f.pump.get_flow_total();

  // Run for 1 decay time constant
  for (int i = 0; i < 500; ++i) {
    f.advance_time_ms(10);
  }

  // Volume should have increased (residual flow delivered extra water)
  EXPECT_GT(f.pump.get_flow_total(), volume_at_stop);
}

TEST(MockPump, ResidualFlowResetClearsSystemPressure) {
  // reset_flow() should clear system pressure so no residual flow after reset.
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.set_internal_volume(20.0f);
  f.pump.turn_on();

  for (int i = 0; i < 5000; ++i) {
    f.advance_time_ms(10);
  }

  f.pump.turn_off();
  f.advance_time_ms(10);  // Let one tick run (residual flow active)
  EXPECT_GT(f.pump.get_flow_rate(), 0.0f);  // Confirm residual flow

  f.pump.reset_flow();  // Clear everything
  EXPECT_FLOAT_EQ(f.pump.get_flow_rate(), 0.0f);
  EXPECT_FLOAT_EQ(f.pump.get_flow_total(), 0.0f);

  // Another loop tick should not restart residual flow
  f.advance_time_ms(10);
  EXPECT_FLOAT_EQ(f.pump.get_flow_rate(), 0.0f);
}

TEST(MockPump, EarlyStopYieldsLessResidualPressure) {
  // Stopping during wetting phase (before steady state) should yield less
  // residual flow than stopping after steady state, because system_pressure
  // tracks wetted_fraction.
  MockPumpFixture f_early(4.0f, 10.0f);
  f_early.pump.set_internal_volume(20.0f);

  MockPumpFixture f_late(4.0f, 10.0f);
  f_late.pump.set_internal_volume(20.0f);

  f_early.pump.turn_on();
  f_late.pump.turn_on();

  // Early: stop after 1 s (well within wetting phase, wetted_fraction ≈ 0.095)
  for (int i = 0; i < 100; ++i) {
    f_early.advance_time_ms(10);
  }
  f_early.pump.turn_off();

  // Late: stop after 50 s (5τ, near steady state)
  for (int i = 0; i < 5000; ++i) {
    f_late.advance_time_ms(10);
  }
  f_late.pump.turn_off();

  // Advance one tick to get the first residual reading
  f_early.advance_time_ms(10);
  f_late.advance_time_ms(10);

  // Early stop should produce less residual flow than late stop
  EXPECT_LT(f_early.pump.get_flow_rate(), f_late.pump.get_flow_rate());
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

// ---------------------------------------------------------------------------
// Pressure sensor tests
//
// system_pressure_bar_ is computed during simulation and exposed via
// get_system_pressure() and the optional pressure_sensor_.
// ---------------------------------------------------------------------------

TEST(MockPump, PressureIsZeroWhenPumpOff) {
  MockPumpFixture f;
  // No pump activity — pressure should be zero at start
  EXPECT_FLOAT_EQ(f.pump.get_system_pressure(), 0.0f);
}

TEST(MockPump, PressureBuildsWhilePumping) {
  // With default params (9 bar puck, τ=10s, P_stall=15 bar):
  // system_pressure = puck_pressure × wetted_fraction
  // After 5τ (≈50s), wetted_fraction ≈ 1.0 → pressure ≈ puck_pressure (9 bar)
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.set_puck_pressure(9.0f);
  f.pump.set_pump_max_pressure(15.0f);
  f.pump.turn_on();

  // Run for 5τ = 50 seconds
  for (int i = 0; i < 5000; ++i) {
    f.advance_time_ms(10);
  }

  // Pressure should be near puck_pressure (9 bar) at steady state
  EXPECT_NEAR(f.pump.get_system_pressure(), 9.0f, 0.5f);
}

TEST(MockPump, PressureStartsNearZeroOnPumpStart) {
  // At pump start, wetted_fraction = 0 so system_pressure should be near 0
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.turn_on();
  f.advance_time_ms(10);  // Very first tick

  // At t=0.01s: wetted_fraction ≈ 1 - exp(-0.01/10) ≈ 0.001
  EXPECT_NEAR(f.pump.get_system_pressure(), 0.0f, 0.1f);
}

TEST(MockPump, PressureDecaysAfterPumpStops) {
  // Pump at steady state then stopped — pressure should decay
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.set_internal_volume(20.0f);  // τ_decay = 5 s
  f.pump.turn_on();

  // Run to steady state (5τ = 50 s)
  for (int i = 0; i < 5000; ++i) {
    f.advance_time_ms(10);
  }
  float peak_pressure = f.pump.get_system_pressure();
  ASSERT_NEAR(peak_pressure, 9.0f, 0.5f);  // Sanity check

  f.pump.turn_off();

  // Run for 2× τ_decay (10 s) — pressure should have decayed significantly
  for (int i = 0; i < 1000; ++i) {
    f.advance_time_ms(10);
  }

  EXPECT_LT(f.pump.get_system_pressure(), peak_pressure * 0.5f);
}

TEST(MockPump, PressureIsZeroAfterReset) {
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.turn_on();

  // Accumulate some pressure
  for (int i = 0; i < 1000; ++i) {
    f.advance_time_ms(10);
  }
  EXPECT_GT(f.pump.get_system_pressure(), 0.0f);

  f.pump.reset_flow();
  EXPECT_FLOAT_EQ(f.pump.get_system_pressure(), 0.0f);
}

TEST(MockPump, PressureSensorPublished) {
  MockPump pump;
  Sensor rate_sensor, total_sensor, pressure_sensor;
  pump.set_nominal_flow(4.0f);
  pump.set_puck_time_constant(0.0f);  // immediate wetting for quick test
  pump.set_pump_max_pressure(15.0f);
  pump.set_puck_pressure(9.0f);
  pump.set_internal_volume(0.0f);
  pump.set_rate_sensor(&rate_sensor);
  pump.set_total_sensor(&total_sensor);
  pump.set_pressure_sensor(&pressure_sensor);

  g_mock_millis = 0;
  pump.setup();
  pump.turn_on();

  // Advance past the 250ms sensor publish threshold
  g_mock_millis = 300;
  pump.loop();

  // With τ=0 (immediate wetting), pressure at steady state = puck_pressure
  EXPECT_NEAR(pressure_sensor.state, 9.0f, 0.5f);
}

TEST(MockPump, PressureSensorPublishesZeroOnReset) {
  MockPump pump;
  Sensor pressure_sensor;
  pump.set_nominal_flow(4.0f);
  pump.set_puck_time_constant(0.0f);
  pump.set_pump_max_pressure(15.0f);
  pump.set_puck_pressure(9.0f);
  pump.set_pressure_sensor(&pressure_sensor);

  g_mock_millis = 0;
  pump.setup();
  pump.turn_on();

  // Use a timestamp well past the 250ms threshold AND beyond the previous test's publish time
  g_mock_millis = 1000;
  pump.loop();
  EXPECT_GT(pressure_sensor.state, 0.0f);  // confirm it was published

  pump.reset_flow();
  EXPECT_FLOAT_EQ(pressure_sensor.state, 0.0f);
}

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
