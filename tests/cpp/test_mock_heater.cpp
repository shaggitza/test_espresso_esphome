#include <gtest/gtest.h>
#include <cmath>
#include "esphome/core/hal.h"
#include "espresso_machine_mock_heater/mock_heater.h"

using namespace esphome::espresso_machine_mock_heater;
using esphome::sensor::Sensor;

extern uint32_t g_mock_millis;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

// Build and setup a MockHeater with given thermal parameters.
static MockHeater make_heater(float initial_temp = 20.0f, float power_w = 1200.0f,
                               float thermal_mass = 1200.0f, float heat_loss = 1.2f,
                               float ambient = 20.0f) {
  MockHeater h;
  h.set_initial_temp(initial_temp);
  h.set_power_watts(power_w);
  h.set_thermal_mass_j_per_c(thermal_mass);
  h.set_heat_loss_w_per_c(heat_loss);
  h.set_ambient_temp(ambient);
  g_mock_millis = 0;
  h.setup();
  return h;
}

// Advance simulated time by n steps of 100 ms, running loop() each step.
static void advance(MockHeater &h, int steps) {
  for (int i = 0; i < steps; i++) {
    g_mock_millis += 100;
    h.loop();
  }
}

// ---------------------------------------------------------------------------
// Initial state
// ---------------------------------------------------------------------------

TEST(MockHeater, InitialTempIsSetCorrectly) {
  MockHeater h = make_heater(25.0f);
  EXPECT_FLOAT_EQ(h.get_current_temp(), 25.0f);
}

TEST(MockHeater, InitialDutyIsZero) {
  MockHeater h = make_heater();
  EXPECT_FLOAT_EQ(h.get_duty(), 0.0f);
}

TEST(MockHeater, SensorReceivesInitialTemp) {
  Sensor s;
  MockHeater h = make_heater(30.0f);
  h.set_temperature_sensor(&s);
  g_mock_millis = 0;
  h.setup();
  EXPECT_FLOAT_EQ(s.state, 30.0f);
}

// ---------------------------------------------------------------------------
// Thermal model — heating
// ---------------------------------------------------------------------------

TEST(MockHeater, FullDutyRaisesTemperature) {
  // At full duty and T == ambient, the ODE simplifies to dT/dt = power/thermal_mass.
  // With P=1200W and C=1200 J/°C: dT/dt = 1 °C/s → after 10 s ≈ +10°C.
  MockHeater h = make_heater(20.0f, 1200.0f, 1200.0f, 0.001f, 20.0f);
  h.set_level(1.0f);  // 100% duty

  float temp_before = h.get_current_temp();
  advance(h, 100);  // 10 seconds
  EXPECT_GT(h.get_current_temp(), temp_before);
}

TEST(MockHeater, ZeroDutyCoolsWhenAboveAmbient) {
  // T starts above ambient; with duty=0 the heater only loses heat.
  MockHeater h = make_heater(80.0f, 1200.0f, 1200.0f, 1.2f, 20.0f);
  h.set_level(0.0f);

  float temp_before = h.get_current_temp();
  advance(h, 100);  // 10 seconds
  EXPECT_LT(h.get_current_temp(), temp_before);
}

TEST(MockHeater, ZeroDutyAtAmbientStaysStable) {
  // T == ambient, duty=0 → no heat in, no heat out → T stays constant.
  MockHeater h = make_heater(20.0f, 1200.0f, 1200.0f, 1.2f, 20.0f);
  h.set_level(0.0f);

  advance(h, 50);  // 5 seconds
  EXPECT_NEAR(h.get_current_temp(), 20.0f, 0.01f);
}

TEST(MockHeater, HigherDutyHeatsMoreThanLowerDuty) {
  // Same conditions, higher duty → higher temperature after equal time.
  MockHeater h1 = make_heater(20.0f);
  MockHeater h2 = make_heater(20.0f);
  h1.set_level(0.5f);
  h2.set_level(0.1f);

  advance(h1, 100);
  advance(h2, 100);
  EXPECT_GT(h1.get_current_temp(), h2.get_current_temp());
}

TEST(MockHeater, HeatingConvergesToSteadyState) {
  // Steady state: T_ss = ambient + duty * power / heat_loss
  // With duty=1, P=100W, h=1 W/°C, ambient=20 → T_ss = 120°C.
  // After many time constants (tau = C/h = 100s), T should be near T_ss.
  MockHeater h = make_heater(20.0f, 100.0f, 100.0f, 1.0f, 20.0f);
  h.set_level(1.0f);

  // Run for ~5 time constants (5 × 100 s = 500 s = 5000 steps of 100 ms)
  advance(h, 5000);
  float expected_ss = 20.0f + 1.0f * 100.0f / 1.0f;  // 120 °C
  EXPECT_NEAR(h.get_current_temp(), expected_ss, 2.0f);
}

TEST(MockHeater, PartialDutyConvergesToLowerSteadyState) {
  // duty=0.1 → T_ss = 20 + 0.1 × 100 / 1.0 = 30 °C
  MockHeater h = make_heater(20.0f, 100.0f, 100.0f, 1.0f, 20.0f);
  h.set_level(0.1f);

  advance(h, 5000);  // 500 s — well past tau = 100 s
  float expected_ss = 20.0f + 0.1f * 100.0f / 1.0f;  // 30 °C
  EXPECT_NEAR(h.get_current_temp(), expected_ss, 0.5f);
}

TEST(MockHeater, SmallerThermalMassHeatsAndCoolsFaster) {
  // Two heaters differ only by thermal_mass.  After equal time the lighter
  // one should be further from its starting point.
  MockHeater heavy = make_heater(20.0f, 100.0f, 1000.0f, 1.0f, 20.0f);
  MockHeater light = make_heater(20.0f, 100.0f, 100.0f, 1.0f, 20.0f);
  heavy.set_level(1.0f);
  light.set_level(1.0f);

  advance(heavy, 200);  // 20 seconds
  advance(light, 200);
  EXPECT_GT(light.get_current_temp(), heavy.get_current_temp());
}

TEST(MockHeater, HeatLossAffectsSteadyState) {
  // Higher heat loss → lower steady-state temperature at the same duty.
  MockHeater low_loss = make_heater(20.0f, 100.0f, 100.0f, 0.5f, 20.0f);
  MockHeater high_loss = make_heater(20.0f, 100.0f, 100.0f, 2.0f, 20.0f);
  low_loss.set_level(1.0f);
  high_loss.set_level(1.0f);

  advance(low_loss, 5000);
  advance(high_loss, 5000);
  EXPECT_GT(low_loss.get_current_temp(), high_loss.get_current_temp());
}

// ---------------------------------------------------------------------------
// Sensor publishing
// ---------------------------------------------------------------------------

TEST(MockHeater, SensorUpdatesAfterEachLoop) {
  Sensor s;
  MockHeater h = make_heater(20.0f);
  h.set_temperature_sensor(&s);
  g_mock_millis = 0;
  h.setup();
  h.set_level(1.0f);

  float initial = s.state;
  advance(h, 20);  // 2 seconds — should trigger at least one sensor update
  EXPECT_GT(s.state, initial);
}

TEST(MockHeater, LoopSkipsUpdateIfIntervalTooShort) {
  Sensor s;
  MockHeater h = make_heater(20.0f);
  h.set_temperature_sensor(&s);
  g_mock_millis = 0;
  h.setup();

  // Publish is done in setup; now advance by < 100 ms — loop() should skip
  float after_setup = s.state;
  g_mock_millis = 50;
  h.set_level(1.0f);
  h.loop();
  // Sensor state must not change — loop returned early
  EXPECT_FLOAT_EQ(s.state, after_setup);
}

// ---------------------------------------------------------------------------
// HA runtime parameter adjustments
// ---------------------------------------------------------------------------

TEST(MockHeater, RuntimeAmbientTempAffectsCooling) {
  // Setting ambient to 25°C means a heater at 20°C should heat slightly
  // when duty=0 (since 20 < 25, heat flows in from "environment").
  MockHeater h = make_heater(20.0f, 100.0f, 100.0f, 1.0f, 20.0f);
  h.set_ambient_temp_runtime(25.0f);  // simulates HA slider writing new value
  h.set_level(0.0f);
  advance(h, 100);
  EXPECT_GT(h.get_current_temp(), 20.0f);  // moved toward new ambient 25 °C
}

TEST(MockHeater, RuntimePowerWattsAffectsHeatRate) {
  // Raise power from 100 W to 500 W — should heat much faster.
  MockHeater baseline = make_heater(20.0f, 100.0f, 100.0f, 0.1f, 20.0f);
  MockHeater boosted = make_heater(20.0f, 100.0f, 100.0f, 0.1f, 20.0f);
  boosted.set_power_watts_runtime(500.0f);

  baseline.set_level(1.0f);
  boosted.set_level(1.0f);

  advance(baseline, 100);  // 10 s
  advance(boosted, 100);
  EXPECT_GT(boosted.get_current_temp(), baseline.get_current_temp());
}

TEST(MockHeater, RuntimeHeatLossAffectsSteadyState) {
  // Reducing heat loss raises the steady-state temperature.
  MockHeater h = make_heater(20.0f, 100.0f, 100.0f, 2.0f, 20.0f);
  h.set_heat_loss_runtime(0.5f);  // simulates HA slider
  h.set_level(1.0f);
  advance(h, 5000);  // wait for convergence
  // New T_ss = 20 + 1.0 * 100 / 0.5 = 220 °C (much higher than original 70 °C)
  EXPECT_GT(h.get_current_temp(), 100.0f);
}

TEST(MockHeater, RuntimeThermalMassIgnoresZero) {
  // A zero thermal mass would cause division by zero — must be silently ignored.
  MockHeater h = make_heater(20.0f, 100.0f, 500.0f, 1.0f, 20.0f);
  h.set_thermal_mass_runtime(0.0f);  // invalid — must be ignored
  h.set_level(1.0f);
  EXPECT_NO_FATAL_FAILURE(advance(h, 50));
  EXPECT_GT(h.get_current_temp(), 20.0f);  // still heats with original mass

}

TEST(MockHeater, ParamNumberPublishesStateOnControl) {
  // MockHeaterParamNumber::control() (called via ESPHome internals) stores
  // the value in publish_state().  Verify by calling publish_state directly.
  MockHeaterParamNumber num;
  // No parent — only checking state storage, not forwarding.
  num.publish_state(42.0f);
  EXPECT_FLOAT_EQ(num.state, 42.0f);
}

TEST(MockHeater, WriteStateSetsInternalDuty) {
  MockHeater h = make_heater();
  h.set_level(0.75f);
  EXPECT_FLOAT_EQ(h.get_duty(), 0.75f);
}
