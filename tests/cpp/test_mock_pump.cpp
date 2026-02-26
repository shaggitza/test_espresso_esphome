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
  Sensor nozzle_rate_sensor;
  Sensor nozzle_total_sensor;

  // Default: nominal_flow=4, tau=10, pump_max=15, density=50 (medium puck)
  // extraction_tau defaults to 0 (no degradation) to keep unit tests deterministic.
  // Use set_puck_extraction_tau() on the pump to test degradation behaviour.
  MockPumpFixture(float nominal_flow = 4.0f, float puck_tau = 10.0f,
                  float pump_max_pressure = 15.0f, float puck_density = 50.0f) {
    pump.set_nominal_flow(nominal_flow);
    pump.set_puck_time_constant(puck_tau);
    pump.set_pump_max_pressure(pump_max_pressure);
    pump.set_puck_density(puck_density);
    pump.set_puck_extraction_tau(0.0f);  // Disable degradation for deterministic tests
    pump.set_rate_sensor(&rate_sensor);
    pump.set_total_sensor(&total_sensor);
    pump.set_nozzle_rate_sensor(&nozzle_rate_sensor);
    pump.set_nozzle_total_sensor(&nozzle_total_sensor);

    g_mock_millis = 0;
    pump.setup();
  }

  void advance_time_ms(uint32_t dt_ms) {
    g_mock_millis += dt_ms;
    pump.loop();
  }
};

// ---------------------------------------------------------------------------
// Physics helpers
//
// With puck_density = D (default 50):
//   flow_fraction = (101 - D) / 100       D=50 → 0.51
//   Q_ss          = nominal_flow × flow_fraction   4 × 0.51 = 2.04 mL/s
//   τ_eff         = puck_time_constant × (D / 100)  10 × 0.5 = 5.0 s
//   P_eq          = pump_max × (D-1) / 100           15 × 0.49 = 7.35 bar
// ---------------------------------------------------------------------------
static float density_flow_fraction(float D) { return (101.0f - D) / 100.0f; }
static float density_q_ss(float nominal, float D) {
  return nominal * density_flow_fraction(D);
}
static float density_tau_eff(float tau, float D) { return tau * (D / 100.0f); }
static float density_p_eq(float pump_max, float D) {
  return pump_max * (D - 1.0f) / 100.0f;
}

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
  MockPumpFixture f(5.0f, 15.0f, 15.0f, 60.0f);
  EXPECT_FLOAT_EQ(f.pump.get_nominal_flow(), 5.0f);
  EXPECT_FLOAT_EQ(f.pump.get_puck_time_constant(), 15.0f);
  EXPECT_FLOAT_EQ(f.pump.get_puck_density(), 60.0f);
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
// Puck density flow model tests
//
// At D=50 (default, medium puck):
//   flow_fraction = 0.51  → Q_ss = 4 × 0.51 = 2.04 mL/s
//   τ_eff = 10 × 0.5 = 5.0 s
//   Q(t) = Q_ss × (1 − exp(−t / τ_eff))
// ---------------------------------------------------------------------------

TEST(MockPump, FlowStartsNearZero) {
  MockPumpFixture f(4.0f, 10.0f);  // D=50 default
  f.pump.turn_on();
  f.advance_time_ms(10);  // 10 ms

  // Q(0.01s) = Q_ss × (1 - exp(-0.01/5)) ≈ 2.04 × 0.002 ≈ 0.004 mL/s
  EXPECT_NEAR(f.pump.get_flow_rate(), 0.004f, 0.01f);
}

TEST(MockPump, FlowRampsExponentially) {
  MockPumpFixture f(4.0f, 10.0f);  // D=50 default
  f.pump.turn_on();

  // Run for 1 second (100 × 10ms steps)
  for (int i = 0; i < 100; ++i) {
    f.advance_time_ms(10);
  }

  // Q(1s) = Q_ss × (1 - exp(-1/τ_eff))
  float q_ss = density_q_ss(4.0f, 50.0f);
  float tau_eff = density_tau_eff(10.0f, 50.0f);
  float expected = q_ss * (1.0f - std::exp(-1.0f / tau_eff));
  EXPECT_NEAR(f.pump.get_flow_rate(), expected, 0.02f);
}

TEST(MockPump, FlowApproachesSteadyStateAfterSeveralTau) {
  MockPumpFixture f(4.0f, 10.0f);  // D=50 default
  f.pump.turn_on();

  // Run for 5×τ_eff = 5×5 = 25 s → Q ≈ Q_ss (99.3%)
  for (int i = 0; i < 2500; ++i) {
    f.advance_time_ms(10);
  }

  float q_ss = density_q_ss(4.0f, 50.0f);  // 2.04 mL/s at D=50
  EXPECT_NEAR(f.pump.get_flow_rate(), q_ss, 0.1f);
}

TEST(MockPump, ZeroTimeConstantGivesImmediateFlow) {
  MockPumpFixture f(4.0f, 0.0f);  // τ=0 → immediate full wetting
  f.pump.turn_on();
  f.advance_time_ms(10);

  // With τ=0: wetted_fraction = 1.0 immediately → Q = Q_ss
  float q_ss = density_q_ss(4.0f, 50.0f);
  EXPECT_NEAR(f.pump.get_flow_rate(), q_ss, 0.01f);
}

// ---------------------------------------------------------------------------
// Puck density variation tests
// ---------------------------------------------------------------------------

TEST(MockPump, HigherDensityReducesSteadyStateFlow) {
  // D=75 (hard puck): Q_ss = 4 × (101-75)/100 = 4 × 0.26 = 1.04 mL/s
  MockPumpFixture f(4.0f, 10.0f, 15.0f, 75.0f);
  f.pump.turn_on();

  // Run for 5×τ_eff = 5 × 10×0.75 = 37.5 s
  for (int i = 0; i < 3750; ++i) {
    f.advance_time_ms(10);
  }

  float q_ss = density_q_ss(4.0f, 75.0f);  // ≈ 1.04 mL/s
  EXPECT_NEAR(f.pump.get_flow_rate(), q_ss, 0.1f);
}

TEST(MockPump, LowerDensityIncreasesFlow) {
  // D=25 (soft puck): Q_ss = 4 × (101-25)/100 = 4 × 0.76 = 3.04 mL/s
  MockPumpFixture f(4.0f, 10.0f, 15.0f, 25.0f);
  f.pump.turn_on();

  // Run for 5×τ_eff = 5 × 10×0.25 = 12.5 s
  for (int i = 0; i < 1250; ++i) {
    f.advance_time_ms(10);
  }

  float q_ss = density_q_ss(4.0f, 25.0f);  // ≈ 3.04 mL/s
  EXPECT_NEAR(f.pump.get_flow_rate(), q_ss, 0.15f);
  // Soft puck gives more flow than medium puck (D=50 → 2.04 mL/s)
  EXPECT_GT(f.pump.get_flow_rate(), density_q_ss(4.0f, 50.0f));
}

TEST(MockPump, HardPuckExtendsWettingPhase) {
  // D=75: τ_eff = 7.5 s; D=50: τ_eff = 5.0 s
  // After 1 s: hard puck flow < medium puck flow (both wetting phase)
  MockPumpFixture f_medium(4.0f, 10.0f, 15.0f, 50.0f);
  MockPumpFixture f_hard(4.0f, 10.0f, 15.0f, 75.0f);

  f_medium.pump.turn_on();
  f_hard.pump.turn_on();

  for (int i = 0; i < 100; ++i) {
    f_medium.advance_time_ms(10);
    f_hard.advance_time_ms(10);
  }

  EXPECT_LT(f_hard.pump.get_flow_rate(), f_medium.pump.get_flow_rate());
}

TEST(MockPump, FullyBlockedPuckGivesMinimalFlow) {
  // D=100: flow_fraction = 0.01 → Q_ss = 4 × 0.01 = 0.04 mL/s (very small)
  MockPumpFixture f(4.0f, 10.0f, 15.0f, 100.0f);
  f.pump.turn_on();

  // Run for 5×τ_eff = 5 × 10 = 50 s
  for (int i = 0; i < 5000; ++i) {
    f.advance_time_ms(10);
  }

  // Should be very small but not zero (unlike old stall model)
  float q_ss = density_q_ss(4.0f, 100.0f);  // ≈ 0.04 mL/s
  EXPECT_NEAR(f.pump.get_flow_rate(), q_ss, 0.02f);
  EXPECT_LT(f.pump.get_flow_rate(), 0.1f);  // Well below useful flow
}

TEST(MockPump, FullyOpenPuckGivesMaxFlow) {
  // D=1: flow_fraction = 1.0 → Q_ss = 4.0 mL/s (nominal)
  MockPumpFixture f(4.0f, 10.0f, 15.0f, 1.0f);
  f.pump.turn_on();

  // τ_eff = 10 × 0.01 = 0.1 s → very fast wetting; run for 2 s
  for (int i = 0; i < 200; ++i) {
    f.advance_time_ms(10);
  }

  EXPECT_NEAR(f.pump.get_flow_rate(), 4.0f, 0.1f);
}

// ---------------------------------------------------------------------------
// Volume accumulation tests
// ---------------------------------------------------------------------------

TEST(MockPump, VolumeAccumulatesWhileRunning) {
  MockPumpFixture f(4.0f, 10.0f);  // D=50
  f.pump.turn_on();

  // Run for 10 seconds
  for (int i = 0; i < 1000; ++i) {
    f.advance_time_ms(10);
  }

  // Volume = ∫₀^10 Q_ss×(1-exp(-t/τ_eff)) dt where Q_ss=2.04, τ_eff=5
  // = Q_ss × [t + τ_eff×exp(-t/τ_eff)]₀^10
  // = 2.04 × [(10 + 5×exp(-2)) - 5] = 2.04 × [5 + 0.677] = 2.04 × 5.677 ≈ 11.6 mL
  EXPECT_GT(f.pump.get_flow_total(), 8.0f);
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
// Nozzle flow sensor tests
//
// The nozzle flow model accounts for puck water absorption:
//   absorption_rate = (puck_absorption_ml / τ_eff) × exp(-t/τ_eff)
//   nozzle_flow = pump_flow - absorption_rate
//
// Early in the shot, absorption rate is high, so nozzle flow < pump flow.
// As the puck saturates, absorption rate → 0 and nozzle flow → pump flow.
// ---------------------------------------------------------------------------

TEST(MockPump, NozzleFlowZeroWhenPumpOff) {
  MockPumpFixture f;
  EXPECT_FLOAT_EQ(f.nozzle_rate_sensor.state, 0.0f);
}

TEST(MockPump, NozzleFlowLessThanPumpFlowDuringWetting) {
  // During the wetting phase, puck absorbs water so nozzle flow < pump flow
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.turn_on();

  // Run for 2s (early in wetting phase)
  for (int i = 0; i < 200; ++i) {
    f.advance_time_ms(10);
  }

  // Advance past the 250ms sensor publish window
  g_mock_millis += 300;
  f.pump.loop();

  // Nozzle rate should be less than pump flow rate due to absorption
  EXPECT_LT(f.pump.get_nozzle_flow_rate(), f.pump.get_flow_rate());
  // But nozzle flow should still be >= 0
  EXPECT_GE(f.pump.get_nozzle_flow_rate(), 0.0f);
}

TEST(MockPump, NozzleFlowApproachesPumpFlowAfterSaturation) {
  // After the puck is saturated (many τ), nozzle flow ≈ pump flow
  MockPumpFixture f(4.0f, 10.0f);  // Uses default puck_density = 50
  f.pump.set_puck_absorption(10.0f);  // Smaller absorption for faster saturation
  f.pump.turn_on();

  // With D=50: τ_eff = puck_time_constant × (D/100) = 10 × 0.5 = 5s
  // Run for 25 seconds (5× τ_eff) to ensure puck is fully saturated
  for (int i = 0; i < 2500; ++i) {
    f.advance_time_ms(10);
  }

  // Advance past the 250ms sensor publish window
  g_mock_millis += 300;
  f.pump.loop();

  // After saturation, nozzle flow should be very close to pump flow
  // Absorption rate = (absorption / τ) × exp(-t/τ) ≈ 0 when t >> τ
  EXPECT_NEAR(f.pump.get_nozzle_flow_rate(), f.pump.get_flow_rate(), 0.05f);
}

TEST(MockPump, NozzleTotalLessThanPumpTotalDueToAbsorption) {
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.set_internal_volume(0.0f);  // No residual flow
  f.pump.turn_on();

  // Run for 10 seconds
  for (int i = 0; i < 1000; ++i) {
    f.advance_time_ms(10);
  }

  // Nozzle total should be less than pump total due to absorption
  // The difference should be approximately the absorbed volume
  EXPECT_LT(f.nozzle_total_sensor.state, f.pump.get_flow_total());
  
  // Pump total - Nozzle total ≈ absorbed volume
  // The absorbed volume depends on the integral of absorption rate over time.
  // With puck_absorption = 36ml, τ_eff = 5s, after 10s:
  // ∫₀^10 (36/5) × exp(-t/5) dt = 36 × [1 - exp(-2)] ≈ 31 ml theoretically
  // But nozzle flow = max(0, pump_flow - absorption_rate), which clips negative values
  // and the integration is discrete, so actual absorbed is less.
  float pump_total = f.pump.get_flow_total();
  float nozzle_total = f.nozzle_total_sensor.state;
  float absorbed_estimate = pump_total - nozzle_total;
  
  // Absorbed should be a significant fraction of what was pumped
  EXPECT_GT(absorbed_estimate, 5.0f);  // Significant absorption occurred
  EXPECT_LT(absorbed_estimate, pump_total);  // But not more than was pumped
}

TEST(MockPump, ZeroAbsorptionMakesNozzleEqualPump) {
  // When puck_absorption = 0, nozzle flow should equal pump flow
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.set_puck_absorption(0.0f);
  f.pump.set_internal_volume(0.0f);
  f.pump.turn_on();

  // Run for 5 seconds
  for (int i = 0; i < 500; ++i) {
    f.advance_time_ms(10);
  }

  // Nozzle total should equal pump total when no absorption
  EXPECT_NEAR(f.pump.get_flow_total(), f.nozzle_total_sensor.state, 0.1f);
  EXPECT_NEAR(f.pump.get_nozzle_flow_rate(), f.pump.get_flow_rate(), 0.01f);
}

TEST(MockPump, NozzleFlowResetOnResetFlow) {
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.set_puck_absorption(5.0f);  // Low absorption so nozzle flow is positive
  f.pump.turn_on();

  // Run long enough for nozzle flow to be positive
  for (int i = 0; i < 1000; ++i) {
    f.advance_time_ms(10);
  }

  // Confirm nozzle total is non-zero
  g_mock_millis += 300;
  f.pump.loop();
  EXPECT_GT(f.nozzle_total_sensor.state, 0.0f);

  f.pump.reset_flow();
  EXPECT_FLOAT_EQ(f.nozzle_total_sensor.state, 0.0f);
  EXPECT_FLOAT_EQ(f.nozzle_rate_sensor.state, 0.0f);
}

// ---------------------------------------------------------------------------
// Reset flow tests
// ---------------------------------------------------------------------------

TEST(MockPump, ResetClearsFlowCounters) {
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.turn_on();

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
// Pressure model tests
//
// New model: pressure rises quickly (τ_rise = 1.5 s) toward P_equilibrium.
//   P_eq = pump_max × (D − 1) / 100
//   D=50 → P_eq = 15 × 0.49 = 7.35 bar
//   D=100 → P_eq = 15 × 0.99 = 14.85 bar (near stall)
//   D=1 → P_eq = 0 bar (no restriction)
// ---------------------------------------------------------------------------

TEST(MockPump, PressureIsZeroWhenPumpOff) {
  MockPumpFixture f;
  EXPECT_FLOAT_EQ(f.pump.get_system_pressure(), 0.0f);
}

TEST(MockPump, PressureBuildsQuicklyToEquilibrium) {
  // With τ_rise = 1.5 s, after 5×τ_rise = 7.5 s pressure ≈ P_eq
  MockPumpFixture f(4.0f, 10.0f, 15.0f, 50.0f);  // D=50
  f.pump.turn_on();

  // Run for 15 s (well past 5×τ_rise)
  for (int i = 0; i < 1500; ++i) {
    f.advance_time_ms(10);
  }

  float p_eq = density_p_eq(15.0f, 50.0f);  // 7.35 bar
  EXPECT_NEAR(f.pump.get_system_pressure(), p_eq, 0.3f);
}

TEST(MockPump, PressureStartsNearZeroOnPumpStart) {
  // τ_rise = 1.5 s → after 10 ms: pressure ≈ P_eq × (1-exp(-0.01/1.5)) ≈ 0
  MockPumpFixture f(4.0f, 10.0f, 15.0f, 50.0f);
  f.pump.turn_on();
  f.advance_time_ms(10);

  EXPECT_NEAR(f.pump.get_system_pressure(), 0.0f, 0.5f);
}

TEST(MockPump, BlockedPuckBuildsPressureToNearStall) {
  // D=100: P_eq = 15 × 0.99 = 14.85 bar → climbs to near pump max
  MockPumpFixture f(4.0f, 10.0f, 15.0f, 100.0f);
  f.pump.turn_on();

  // Run for 5×τ_rise = 7.5 s
  for (int i = 0; i < 750; ++i) {
    f.advance_time_ms(10);
  }

  float p_eq = density_p_eq(15.0f, 100.0f);  // ≈ 14.85 bar
  EXPECT_NEAR(f.pump.get_system_pressure(), p_eq, 0.5f);
  EXPECT_GT(f.pump.get_system_pressure(), 13.0f);  // Near stall
}

TEST(MockPump, OpenPuckHasMinimalPressure) {
  // D=1: P_eq = 0 bar → no back-pressure, pressure stays near 0
  MockPumpFixture f(4.0f, 10.0f, 15.0f, 1.0f);
  f.pump.turn_on();

  for (int i = 0; i < 1500; ++i) {
    f.advance_time_ms(10);
  }

  EXPECT_NEAR(f.pump.get_system_pressure(), 0.0f, 0.1f);
}

TEST(MockPump, PressureDecaysAfterPumpStops) {
  // Pump at steady state then stopped — pressure should decay
  MockPumpFixture f(4.0f, 10.0f, 15.0f, 50.0f);
  f.pump.set_internal_volume(20.0f);  // τ_decay = 5 s
  f.pump.turn_on();

  // Run to pressure equilibrium (15 s >> 5×τ_rise = 7.5 s)
  for (int i = 0; i < 1500; ++i) {
    f.advance_time_ms(10);
  }
  float peak_pressure = f.pump.get_system_pressure();
  float p_eq = density_p_eq(15.0f, 50.0f);  // 7.35 bar
  ASSERT_NEAR(peak_pressure, p_eq, 0.5f);

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
  pump.set_puck_time_constant(0.0f);   // immediate wetting
  pump.set_pump_max_pressure(15.0f);
  pump.set_puck_density(50.0f);        // P_eq = 7.35 bar
  pump.set_internal_volume(0.0f);
  pump.set_puck_extraction_tau(0.0f);  // disable degradation for deterministic P_eq
  pump.set_rate_sensor(&rate_sensor);
  pump.set_total_sensor(&total_sensor);
  pump.set_pressure_sensor(&pressure_sensor);

  g_mock_millis = 0;
  pump.setup();
  pump.turn_on();

  // Run past 5×τ_rise = 7.5 s and past the 250 ms sensor publish threshold
  for (uint32_t t = 10; t <= 8000; t += 10) {
    g_mock_millis = t;
    pump.loop();
  }

  // Pressure should be near P_eq = 7.35 bar
  float p_eq = density_p_eq(15.0f, 50.0f);
  EXPECT_NEAR(pressure_sensor.state, p_eq, 0.5f);
}

TEST(MockPump, PressureSensorPublishesZeroOnReset) {
  MockPump pump;
  Sensor pressure_sensor;
  pump.set_nominal_flow(4.0f);
  pump.set_puck_time_constant(0.0f);
  pump.set_pump_max_pressure(15.0f);
  pump.set_puck_density(50.0f);
  pump.set_puck_extraction_tau(0.0f);  // disable degradation for deterministic P_eq
  pump.set_pressure_sensor(&pressure_sensor);

  g_mock_millis = 0;
  pump.setup();
  pump.turn_on();

  for (uint32_t t = 10; t <= 8000; t += 10) {
    g_mock_millis = t;
    pump.loop();
  }
  EXPECT_GT(pressure_sensor.state, 0.0f);  // confirm it was published

  pump.reset_flow();
  EXPECT_FLOAT_EQ(pressure_sensor.state, 0.0f);
}

// ---------------------------------------------------------------------------
// Residual pressure-driven flow tests
//
// When internal_volume_ml > 0 the mock simulates the trapped pressure in
// tubing/piping continuing to drive flow after the pump stops.
// τ_decay = internal_volume_ml / nominal_flow  (e.g. 20/4 = 5 s)
// ---------------------------------------------------------------------------

TEST(MockPump, ResidualFlowDecreasesGraduallyWithInternalVolume) {
  // With internal_volume = 20, τ_decay = 5 s.
  // After pump stops at steady state, flow should still be measurable
  // at 1 τ (37% of steady-state) rather than snapping to zero.
  MockPumpFixture f(4.0f, 10.0f, 15.0f, 50.0f);
  f.pump.set_internal_volume(20.0f);
  f.pump.turn_on();

  // Run to flow steady state (5×τ_eff = 25 s) and pressure equilibrium
  for (int i = 0; i < 2500; ++i) {
    f.advance_time_ms(10);
  }
  float steady_flow = f.pump.get_flow_rate();
  float q_ss = density_q_ss(4.0f, 50.0f);
  ASSERT_NEAR(steady_flow, q_ss, 0.15f);  // Sanity-check steady state

  // Stop pump
  f.pump.turn_off();

  // Immediately after stop, flow should still be positive
  f.advance_time_ms(10);
  EXPECT_GT(f.pump.get_flow_rate(), 0.0f);

  // After 1 τ_decay (5 s), flow should be ≥ 25% of steady state
  for (int i = 0; i < 499; ++i) {
    f.advance_time_ms(10);
  }
  EXPECT_GT(f.pump.get_flow_rate(), steady_flow * 0.25f);
}

TEST(MockPump, ResidualFlowEventuallyReachesZero) {
  // After many decay time constants, flow must be essentially zero.
  MockPumpFixture f(4.0f, 10.0f, 15.0f, 50.0f);
  f.pump.set_internal_volume(20.0f);
  f.pump.turn_on();

  // Run to flow steady state
  for (int i = 0; i < 2500; ++i) {
    f.advance_time_ms(10);
  }

  f.pump.turn_off();

  // Run for 10× τ_decay (50 s): flow should be < 1% of steady state
  for (int i = 0; i < 5000; ++i) {
    f.advance_time_ms(10);
  }

  EXPECT_NEAR(f.pump.get_flow_rate(), 0.0f, 0.05f);
}

TEST(MockPump, LargerInternalVolumeSlowsDecay) {
  // Larger internal_volume → longer τ_decay → higher flow after equal time.
  MockPumpFixture f_small(4.0f, 10.0f, 15.0f, 50.0f);
  f_small.pump.set_internal_volume(10.0f);  // τ = 2.5 s

  MockPumpFixture f_large(4.0f, 10.0f, 15.0f, 50.0f);
  f_large.pump.set_internal_volume(40.0f);  // τ = 10 s

  f_small.pump.turn_on();
  f_large.pump.turn_on();

  // Run both to flow steady state (25 s)
  for (int i = 0; i < 2500; ++i) {
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
  // Disabling pressure model (internal_volume = 0) gives legacy quick decay.
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.set_internal_volume(0.0f);
  f.pump.turn_on();

  // Run to steady state
  for (int i = 0; i < 2500; ++i) {
    f.advance_time_ms(10);
  }

  f.pump.turn_off();

  // After 2 s (200 steps) with no internal volume, flow should be < 1%.
  // Each step multiplies by 0.9: 0.9^200 ≈ 7e-10 ≈ 0.
  for (int i = 0; i < 200; ++i) {
    f.advance_time_ms(10);
  }

  EXPECT_NEAR(f.pump.get_flow_rate(), 0.0f, 0.01f);
}

TEST(MockPump, ResidualFlowAccumulatesVolume) {
  // Volume should keep accumulating after pump stops while pressure decays.
  MockPumpFixture f(4.0f, 10.0f, 15.0f, 50.0f);
  f.pump.set_internal_volume(20.0f);
  f.pump.turn_on();

  // Run to steady state
  for (int i = 0; i < 2500; ++i) {
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
  MockPumpFixture f(4.0f, 10.0f, 15.0f, 50.0f);
  f.pump.set_internal_volume(20.0f);
  f.pump.turn_on();

  for (int i = 0; i < 2500; ++i) {
    f.advance_time_ms(10);
  }

  f.pump.turn_off();
  f.advance_time_ms(10);
  EXPECT_GT(f.pump.get_flow_rate(), 0.0f);  // Confirm residual flow

  f.pump.reset_flow();  // Clear everything
  EXPECT_FLOAT_EQ(f.pump.get_flow_rate(), 0.0f);
  EXPECT_FLOAT_EQ(f.pump.get_flow_total(), 0.0f);

  // Another loop tick should not restart residual flow
  f.advance_time_ms(10);
  EXPECT_FLOAT_EQ(f.pump.get_flow_rate(), 0.0f);
}

TEST(MockPump, EarlyStopYieldsLessResidualFlow) {
  // Stopping during pressure buildup phase yields less residual flow than
  // stopping after pressure reaches equilibrium.
  // P_rise: after 1 s at τ=1.5 s → P ≈ P_eq × 0.49
  //         after 15 s               → P ≈ P_eq × 1.00
  MockPumpFixture f_early(4.0f, 10.0f, 15.0f, 50.0f);
  f_early.pump.set_internal_volume(20.0f);

  MockPumpFixture f_late(4.0f, 10.0f, 15.0f, 50.0f);
  f_late.pump.set_internal_volume(20.0f);

  f_early.pump.turn_on();
  f_late.pump.turn_on();

  // Early: stop after 1 s (pressure still building)
  for (int i = 0; i < 100; ++i) {
    f_early.advance_time_ms(10);
  }
  f_early.pump.turn_off();

  // Late: stop after 25 s (flow AND pressure at steady state)
  for (int i = 0; i < 2500; ++i) {
    f_late.advance_time_ms(10);
  }
  f_late.pump.turn_off();

  // Advance one tick to get first residual reading
  f_early.advance_time_ms(10);
  f_late.advance_time_ms(10);

  // Early stop → less residual flow (lower trapped pressure)
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

  f.pump.update_puck_density(75.0f);
  EXPECT_FLOAT_EQ(f.pump.get_puck_density(), 75.0f);
}

TEST(MockPump, ChangingNominalFlowAffectsFutureRate) {
  MockPumpFixture f(4.0f, 10.0f);
  f.pump.turn_on();

  for (int i = 0; i < 100; ++i) {
    f.advance_time_ms(10);
  }

  float rate_before = f.pump.get_flow_rate();

  // Double the nominal flow
  f.pump.update_nominal_flow(8.0f);

  for (int i = 0; i < 100; ++i) {
    f.advance_time_ms(10);
  }

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
  f.advance_time_ms(100);
  f.pump.turn_on();

  // After restart, run time is reset so flow starts ramping from near zero
  f.advance_time_ms(10);

  EXPECT_LT(f.pump.get_flow_rate(), rate_before);
  EXPECT_GT(f.pump.get_flow_total(), volume_before - 0.1f);
}

// ---------------------------------------------------------------------------
// Puck extraction / degradation model tests
//
// When puck_extraction_tau > 0, the effective puck density decreases over time:
//   D_eff(t) = 1 + (D − 1) × exp(−t / τ_extract)
//
// This models coffee solubles dissolving and the puck structure weakening,
// causing flow rate to increase monotonically throughout the shot.
// ---------------------------------------------------------------------------

TEST(MockPump, FlowIncreasesOverTimeWithDegradation) {
  // With degradation enabled, flow should continue to increase past Q_ss
  // even after the wetting phase is complete.
  MockPumpFixture f(4.0f, 10.0f, 15.0f, 50.0f);
  f.pump.set_puck_extraction_tau(45.0f);
  f.pump.turn_on();

  // Let wetting phase complete: τ_eff = puck_tau × (D/100) = 10 × 0.5 = 5s → 5×τ_eff = 25s
  for (int i = 0; i < 2500; ++i) {
    f.advance_time_ms(10);
  }
  float flow_after_wetting = f.pump.get_flow_rate();

  // Run for another 30 s — degradation should increase flow further
  for (int i = 0; i < 3000; ++i) {
    f.advance_time_ms(10);
  }
  float flow_after_degradation = f.pump.get_flow_rate();

  // Flow must be strictly higher after further degradation
  EXPECT_GT(flow_after_degradation, flow_after_wetting);
}

TEST(MockPump, DegradationFlowAlwaysIncreasing) {
  // Verify that flow is strictly monotonically increasing throughout a shot
  // when degradation is enabled (after the brief initial wetting transient).
  MockPumpFixture f(4.0f, 10.0f, 15.0f, 50.0f);
  f.pump.set_puck_extraction_tau(45.0f);
  f.pump.turn_on();

  // Skip the very first few ms where wetting starts from 0
  for (int i = 0; i < 10; ++i) {
    f.advance_time_ms(10);
  }

  float prev_flow = f.pump.get_flow_rate();

  // Sample flow every 2 s from t=0.1s to t=60s and verify it keeps rising
  for (int s = 0; s < 30; ++s) {
    for (int i = 0; i < 200; ++i) {
      f.advance_time_ms(10);
    }
    float current_flow = f.pump.get_flow_rate();
    EXPECT_GE(current_flow, prev_flow - 0.001f)
        << "Flow decreased at t=" << (0.1f + s * 2.0f) << "s";
    prev_flow = current_flow;
  }
}

TEST(MockPump, DegradationFlowExceedsInitialQss) {
  // After significant extraction time, flow should exceed the initial Q_ss
  // (the steady-state flow computed from the initial puck density).
  // D=50 → initial Q_ss = 4 × (101-50)/100 = 4 × 0.51 = 2.04 mL/s
  // After 45s (1×τ_extract): D_eff = 1 + 49×exp(-1) ≈ 19.0 → Q_ss ≈ 3.28 mL/s > 2.04
  MockPumpFixture f(4.0f, 10.0f, 15.0f, 50.0f);
  f.pump.set_puck_extraction_tau(45.0f);
  f.pump.turn_on();

  // Run for 50 s (past 1×τ_extract = 45 s)
  for (int i = 0; i < 5000; ++i) {
    f.advance_time_ms(10);
  }

  float initial_q_ss = density_q_ss(4.0f, 50.0f);  // 2.04 mL/s at D=50
  EXPECT_GT(f.pump.get_flow_rate(), initial_q_ss);
}

TEST(MockPump, DegradationDisabledWhenTauIsZero) {
  // When puck_extraction_tau = 0, flow should plateau at Q_ss
  // (same as existing behaviour — this verifies backward compatibility).
  MockPumpFixture f(4.0f, 10.0f, 15.0f, 50.0f);
  // f.pump.set_puck_extraction_tau(0.0f);  // already set to 0 by fixture
  f.pump.turn_on();

  // Run to flow steady state (5×τ_eff = 25 s)
  for (int i = 0; i < 2500; ++i) {
    f.advance_time_ms(10);
  }
  float flow_at_25s = f.pump.get_flow_rate();

  // Run for another 25 s — without degradation, flow should not increase
  for (int i = 0; i < 2500; ++i) {
    f.advance_time_ms(10);
  }
  float flow_at_50s = f.pump.get_flow_rate();

  float q_ss = density_q_ss(4.0f, 50.0f);
  // Both readings should be near Q_ss (no degradation)
  EXPECT_NEAR(flow_at_25s, q_ss, 0.1f);
  EXPECT_NEAR(flow_at_50s, q_ss, 0.1f);
  // And they should be essentially equal (no degradation drift)
  EXPECT_NEAR(flow_at_50s, flow_at_25s, 0.05f);
}

TEST(MockPump, DegradationEffectivelyOpenAtLargeTime) {
  // At t >> τ_extract, effective density → 1, so flow → nominal_flow
  // Use a fast extraction tau for practical test timing
  MockPumpFixture f(4.0f, 10.0f, 15.0f, 50.0f);
  f.pump.set_puck_extraction_tau(5.0f);  // Fast degradation for test
  f.pump.turn_on();

  // Run for 5×τ_extract = 25 s — puck should be nearly fully extracted
  for (int i = 0; i < 2500; ++i) {
    f.advance_time_ms(10);
  }

  // At t=25s with τ_extract=5s: D_eff = 1 + 49×exp(-5) ≈ 1 + 49×0.0067 ≈ 1.33
  // Q_ss = 4 × (101-1.33)/100 ≈ 4 × 0.997 ≈ 3.99 mL/s
  // Flow should be close to nominal_flow (4 mL/s) after full degradation
  EXPECT_GT(f.pump.get_flow_rate(), 3.5f);
}

TEST(MockPump, DegradationResetsOnPumpRestart) {
  // When the pump restarts (run_time_ reset to 0), degradation restarts from
  // the initial puck density (extraction tau resets to the fresh puck).
  MockPumpFixture f(4.0f, 10.0f, 15.0f, 50.0f);
  f.pump.set_puck_extraction_tau(10.0f);  // Moderate degradation
  f.pump.turn_on();

  // Run for 20 s to degrade the puck significantly
  for (int i = 0; i < 2000; ++i) {
    f.advance_time_ms(10);
  }
  float flow_after_degradation = f.pump.get_flow_rate();

  // Stop and restart — run_time_ resets to 0 → fresh puck density applied
  f.pump.turn_off();
  f.advance_time_ms(50);
  f.pump.turn_on();
  f.advance_time_ms(50);  // Very start of new shot

  // Flow should have dropped back toward initial Q_ss (fresh puck)
  float initial_q_ss = density_q_ss(4.0f, 50.0f);
  EXPECT_LT(f.pump.get_flow_rate(), flow_after_degradation);
  // (Flow is near 0 because wetting just started again — just confirm it's
  //  lower than the degraded value.)
}

TEST(MockPump, DegradationRuntimeUpdate) {
  // Verify set_puck_extraction_tau accessor round-trips correctly.
  MockPumpFixture f;
  f.pump.set_puck_extraction_tau(60.0f);
  EXPECT_FLOAT_EQ(f.pump.get_puck_extraction_tau(), 60.0f);

  f.pump.update_puck_extraction_tau(30.0f);
  EXPECT_FLOAT_EQ(f.pump.get_puck_extraction_tau(), 30.0f);
}

TEST(MockPump, PressureAlsoDecreasesDuringDegradation) {
  // With degradation, P_equilibrium decreases as D_eff drops.
  // After significant extraction, the system pressure should stabilise
  // LOWER than the initial P_eq computed from puck_density.
  MockPumpFixture f(4.0f, 10.0f, 15.0f, 50.0f);
  f.pump.set_puck_extraction_tau(10.0f);  // Fast degradation for test
  f.pump.turn_on();

  // Run just past τ_rise (say 10 s) so pressure has stabilised
  // but also enough degradation has occurred (1×τ_extract)
  for (int i = 0; i < 1000; ++i) {
    f.advance_time_ms(10);
  }

  // P_eq at D=50 (initial) = 15 × 0.49 = 7.35 bar
  // After 10s with τ_extract=10s: D_eff = 1 + 49 × exp(-1) ≈ 19.0
  // P_eq at D_eff=19.0 = 15 × 0.18 = 2.7 bar  → significantly lower
  float initial_p_eq = density_p_eq(15.0f, 50.0f);  // 7.35 bar
  EXPECT_LT(f.pump.get_system_pressure(), initial_p_eq);
}


// ---------------------------------------------------------------------------
// MockPump — flow-rate bang-bang control (set_target_flow)
//
// The mock pump implements IPump::set_target_flow() so the orchestrator can
// delegate bang-bang control to it, just as it does with the real PumpSwitch.
// ---------------------------------------------------------------------------

struct MockPumpTargetFlowFixture {
  // Open-valve (bypass) mode: D=1, τ=0 → instant full flow = nominal_flow
  // This avoids waiting for puck wetting in target-flow tests.
  MockPumpTargetFlowFixture() {
    g_mock_millis = 0;
    pump.set_nominal_flow(4.0f);
    pump.set_puck_time_constant(0.0f);  // instant wetting in bypass
    pump.set_puck_density(1.0f);        // no resistance
    pump.set_pump_max_pressure(15.0f);
    pump.set_internal_volume(0.0f);     // no residual pressure
    pump.set_puck_absorption(0.0f);
    pump.set_puck_extraction_tau(0.0f);
    pump.set_bypass_mode(true);         // steam-like: open valve, full flow
    pump.setup();
  }

  void advance_time_ms(uint32_t ms) {
    g_mock_millis += ms;
    pump.loop();
  }

  MockPump pump;
};

TEST(MockPumpTargetFlow, InitialTargetFlowIsZero) {
  MockPumpTargetFlowFixture f;
  EXPECT_FLOAT_EQ(f.pump.get_target_flow(), 0.0f);
}

TEST(MockPumpTargetFlow, SetTargetFlowStoresValue) {
  MockPumpTargetFlowFixture f;
  f.pump.set_target_flow(2.0f);
  EXPECT_FLOAT_EQ(f.pump.get_target_flow(), 2.0f);
}

TEST(MockPumpTargetFlow, PumpTurnsOnWhenRateBelowTarget) {
  MockPumpTargetFlowFixture f;
  // Target is 2 ml/s; pump is off so current_flow_rate = 0 < target → should start.
  f.pump.set_target_flow(2.0f);
  f.advance_time_ms(10);
  EXPECT_TRUE(f.pump.is_running());
}

TEST(MockPumpTargetFlow, PumpTurnsOffWhenRateReachesTarget) {
  // In bypass mode with nominal_flow=4 ml/s and instant wetting, the pump
  // reaches full flow on the first tick. A target of 1 ml/s should cause
  // the pump to immediately shut off after reaching the target.
  MockPumpTargetFlowFixture f;
  // Set a target lower than nominal_flow so the pump overshoots and shuts off.
  f.pump.set_target_flow(1.0f);  // target < 4 ml/s nominal
  f.pump.turn_on();              // start so first tick computes flow
  f.advance_time_ms(10);         // flow = nominal_flow = 4 > target → turns off
  EXPECT_FALSE(f.pump.is_running());
}

TEST(MockPumpTargetFlow, ClearTargetFlowDisablesBangBang) {
  MockPumpTargetFlowFixture f;
  f.pump.set_target_flow(2.0f);
  f.advance_time_ms(10);         // turns on (rate 0 < target)
  EXPECT_TRUE(f.pump.is_running());

  f.pump.set_target_flow(0.0f);  // disable flow control
  f.pump.turn_off();             // external stop
  f.advance_time_ms(10);         // loop must not restart the pump
  EXPECT_FALSE(f.pump.is_running());
}
