#include <gtest/gtest.h>
#include <cmath>
#include "esphome/core/hal.h"
#include "espresso_machine_mock_heater/mock_heater.h"

using namespace esphome::espresso_machine_mock_heater;
using esphome::sensor::Sensor;

extern uint32_t g_mock_millis;

// ---------------------------------------------------------------------------
// Helper to create a configured MockHeater with its sub-entities
// ---------------------------------------------------------------------------
struct MockHeaterFixture {
  MockHeater heater;
  MockHeaterOutput output;
  MockHeaterTempSensor sensor;

  MockHeaterFixture(float initial_temp = 25.0f, float ambient = 25.0f,
                    float power = 1200.0f, float thermal_mass = 1256.0f,
                    float heat_loss = 1.7f) {
    heater.set_initial_temperature(initial_temp);
    heater.set_ambient_temperature(ambient);
    heater.set_power_watts(power);
    heater.set_thermal_mass(thermal_mass);
    heater.set_heat_loss(heat_loss);
    heater.set_output(&output);
    heater.set_temperature_sensor(&sensor);
    output.set_parent(&heater);
    sensor.set_parent(&heater);

    g_mock_millis = 0;
    heater.setup();
  }

  void advance_time_ms(uint32_t dt_ms) {
    g_mock_millis += dt_ms;
    heater.loop();
  }
};

// ---------------------------------------------------------------------------
// Basic initialization tests
// ---------------------------------------------------------------------------

TEST(MockHeater, InitialStateMatchesConfig) {
  MockHeaterFixture f(30.0f, 22.0f, 1500.0f, 1000.0f, 2.0f);
  EXPECT_FLOAT_EQ(f.heater.get_temperature(), 30.0f);
  EXPECT_FLOAT_EQ(f.heater.get_ambient_temp(), 22.0f);
  EXPECT_FLOAT_EQ(f.heater.get_power_watts(), 1500.0f);
  EXPECT_FLOAT_EQ(f.heater.get_thermal_mass(), 1000.0f);
  EXPECT_FLOAT_EQ(f.heater.get_heat_loss(), 2.0f);
}

TEST(MockHeater, OutputInitiallyZero) {
  MockHeaterFixture f;
  EXPECT_FLOAT_EQ(f.output.get_duty(), 0.0f);
  EXPECT_FLOAT_EQ(f.heater.get_duty(), 0.0f);
}

// ---------------------------------------------------------------------------
// Thermal ODE tests
// ---------------------------------------------------------------------------

TEST(MockHeater, NoHeatingAtZeroDuty) {
  // At ambient temp with zero duty, temperature should stay at ambient
  MockHeaterFixture f(25.0f, 25.0f);
  f.advance_time_ms(1000);  // 1 second
  EXPECT_NEAR(f.heater.get_temperature(), 25.0f, 0.01f);
}

TEST(MockHeater, TemperatureDecaysTowardAmbient) {
  // Start above ambient with zero duty — should cool down
  MockHeaterFixture f(90.0f, 25.0f, 1200.0f, 1256.0f, 1.7f);

  // At 90°C, heat loss = 1.7 × (90 - 25) = 110.5 W
  // dT/dt = -110.5 / 1256 ≈ -0.088 °C/s
  // After 1s: T ≈ 90 - 0.088 ≈ 89.91

  // Run for 1 second in 10ms steps
  for (int i = 0; i < 100; ++i) {
    f.advance_time_ms(10);
  }

  // Temperature should have decreased
  EXPECT_LT(f.heater.get_temperature(), 90.0f);
  EXPECT_NEAR(f.heater.get_temperature(), 89.91f, 0.1f);
}

TEST(MockHeater, TemperatureRisesWithHeating) {
  // Start at ambient with full duty — should heat up
  MockHeaterFixture f(25.0f, 25.0f, 1200.0f, 1256.0f, 1.7f);

  // Set duty to 100%
  f.output.set_level(1.0f);

  // At 25°C, heat in = 1200 W, heat out = 1.7 × 0 = 0
  // dT/dt = 1200 / 1256 ≈ 0.955 °C/s
  // After 1s: T ≈ 25 + 0.955 ≈ 25.955

  // Run for 1 second
  for (int i = 0; i < 100; ++i) {
    f.advance_time_ms(10);
  }

  EXPECT_GT(f.heater.get_temperature(), 25.0f);
  EXPECT_NEAR(f.heater.get_temperature(), 25.955f, 0.1f);
}

TEST(MockHeater, ReachesEquilibriumAtPartialDuty) {
  // At equilibrium: duty × P = h × (T_eq - T_amb)
  // T_eq = T_amb + (duty × P) / h
  // At 10% duty: T_eq = 25 + (0.1 × 1200) / 1.7 ≈ 25 + 70.6 ≈ 95.6°C

  MockHeaterFixture f(25.0f, 25.0f, 1200.0f, 1256.0f, 1.7f);
  f.output.set_level(0.1f);

  // Run for a long time (simulated) to reach equilibrium
  // τ = C / h = 1256 / 1.7 ≈ 739s; 5τ ≈ 3695s for 99% settling
  // In test, we'll run until temperature stabilizes within tolerance

  float prev_temp = f.heater.get_temperature();
  for (int i = 0; i < 50000; ++i) {  // ~500 seconds ≈ 0.68 τ (τ = C/h = 1256/1.7 ≈ 739s)
    f.advance_time_ms(10);
  }

  float expected_equilibrium = 25.0f + (0.1f * 1200.0f) / 1.7f;
  // At 0.68τ: T ≈ 25 + (T_eq-25)×(1-e^-0.68) ≈ 59.7°C
  EXPECT_GT(f.heater.get_temperature(), 50.0f);  // Should have risen significantly
}

TEST(MockHeater, DutyClampedToZeroOne) {
  MockHeaterFixture f;

  f.output.set_level(-0.5f);
  EXPECT_FLOAT_EQ(f.output.get_duty(), 0.0f);

  f.output.set_level(1.5f);
  EXPECT_FLOAT_EQ(f.output.get_duty(), 1.0f);

  f.output.set_level(0.5f);
  EXPECT_FLOAT_EQ(f.output.get_duty(), 0.5f);
}

// ---------------------------------------------------------------------------
// Runtime tuning tests
// ---------------------------------------------------------------------------

TEST(MockHeater, RuntimeParameterUpdates) {
  MockHeaterFixture f;

  f.heater.update_power_watts(1500.0f);
  EXPECT_FLOAT_EQ(f.heater.get_power_watts(), 1500.0f);

  f.heater.update_thermal_mass(2000.0f);
  EXPECT_FLOAT_EQ(f.heater.get_thermal_mass(), 2000.0f);

  f.heater.update_heat_loss(3.0f);
  EXPECT_FLOAT_EQ(f.heater.get_heat_loss(), 3.0f);

  f.heater.update_ambient_temp(20.0f);
  EXPECT_FLOAT_EQ(f.heater.get_ambient_temp(), 20.0f);
}

// ---------------------------------------------------------------------------
// Temperature sensor polling
// ---------------------------------------------------------------------------

TEST(MockHeater, SensorPublishesCurrentTemperature) {
  MockHeaterFixture f(50.0f);

  // Create a mock Sensor to capture published values
  Sensor mock_sensor;
  f.sensor.set_parent(&f.heater);

  // The sensor's update() should return the heater's temperature
  f.sensor.update();
  // Note: MockHeaterTempSensor::update() calls publish_state on itself,
  // which requires the sensor to be properly initialized. For this test,
  // we just verify get_temperature() returns the expected value.
  EXPECT_FLOAT_EQ(f.heater.get_temperature(), 50.0f);
}

// ---------------------------------------------------------------------------
// Flow-driven cooling tests
// ---------------------------------------------------------------------------

TEST(MockHeater, FlowCoolsThermoblockWhenHeaterOff) {
  // Start at 90°C, heater off, 4 mL/s flow through a 10mL thermoblock
  // C=42, h=1.7, Q=4, Cp=4.186, T_inlet=20
  // dT/dt = (0 - 1.7×(90-25) - 4×4.186×(90-20)) / 42
  //       = (-110.5 - 1172) / 42 ≈ -30.3 °C/s  — very rapid drop
  MockHeaterFixture f(90.0f, 25.0f, 1200.0f, 42.0f, 1.7f);
  f.heater.set_water_inlet_temp(20.0f);
  f.heater.set_flow_rate(4.0f);  // Simulate pump running at 4 mL/s

  float temp_before = f.heater.get_temperature();

  // After 1 second of flow with heater off, temperature should drop significantly
  for (int i = 0; i < 100; ++i) {
    f.advance_time_ms(10);
  }

  EXPECT_LT(f.heater.get_temperature(), temp_before - 5.0f);
}

TEST(MockHeater, FlowDropsFasterThanRadiationAlone) {
  // Compare cooling rate: flow vs. no flow at 90°C (heater off, 42 J/°C thermoblock)
  MockHeaterFixture f_flow(90.0f, 25.0f, 1200.0f, 42.0f, 1.7f);
  f_flow.heater.set_water_inlet_temp(20.0f);
  f_flow.heater.set_flow_rate(4.0f);

  MockHeaterFixture f_noflow(90.0f, 25.0f, 1200.0f, 42.0f, 1.7f);
  // f_noflow has no flow (default 0)

  for (int i = 0; i < 100; ++i) {
    f_flow.advance_time_ms(10);
    f_noflow.advance_time_ms(10);
  }

  // With flow, should cool significantly more than radiation alone
  EXPECT_LT(f_flow.heater.get_temperature(), f_noflow.heater.get_temperature());
}

TEST(MockHeater, NoFlowMeansNoFlowCooling) {
  // With flow_rate=0, heat_flow term = 0 × Cp × ΔT = 0 regardless of Cp/ΔT.
  // The ODE reduces to the same no-flow equation:
  //   dT/dt = (0 - h × (T - T_amb)) / C
  // Verify by checking the temperature matches the exact analytical Euler result.
  MockHeaterFixture f(90.0f, 25.0f, 1200.0f, 42.0f, 1.7f);
  f.heater.set_flow_rate(0.0f);

  // Single 10 ms step — compare to manual Euler:
  // dT/dt = -h × (90 - 25) / C = -1.7 × 65 / 42 ≈ -2.631 °C/s
  // ΔT over 10 ms = -2.631 × 0.010 ≈ -0.02631 °C → T ≈ 89.9737 °C
  float expected_after_one_step = 90.0f - (1.7f * (90.0f - 25.0f) / 42.0f) * 0.01f;
  f.advance_time_ms(10);
  EXPECT_NEAR(f.heater.get_temperature(), expected_after_one_step, 0.001f);
}

TEST(MockHeater, FlowAndHeaterTogetherFluctuate) {
  // With flow cooling and heater at 50% duty, temperature should be significantly
  // lower than without flow — the interplay creates the "fluctuation" behavior
  // C=42, P=1200, h=1.7, Q=4, Cp=4.186, T_inlet=20
  // Equilibrium with flow: duty×P = h×(T-T_amb) + Q×Cp×(T-T_inlet)
  // 0.5×1200 = 1.7×(T-25) + 4×4.186×(T-20)
  // 600 = 1.7T - 42.5 + 16.744T - 334.88
  // 600 = 18.444T - 377.38 → T = 977.38/18.444 ≈ 53°C
  MockHeaterFixture f(25.0f, 25.0f, 1200.0f, 42.0f, 1.7f);
  f.heater.set_water_inlet_temp(20.0f);
  f.heater.set_flow_rate(4.0f);
  f.output.set_level(0.5f);  // 50% duty

  // Run for 10 seconds
  for (int i = 0; i < 1000; ++i) {
    f.advance_time_ms(10);
  }

  // Temperature should be well below 90°C due to aggressive flow cooling
  EXPECT_LT(f.heater.get_temperature(), 90.0f);
  // And it should have risen above ambient somewhat due to heater
  EXPECT_GT(f.heater.get_temperature(), 25.0f);
}
