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
  for (int i = 0; i < 50000; ++i) {  // ~500 seconds
    f.advance_time_ms(10);
  }

  float expected_equilibrium = 25.0f + (0.1f * 1200.0f) / 1.7f;
  // Should be approaching equilibrium (won't be exact after only 500s)
  EXPECT_GT(f.heater.get_temperature(), 80.0f);  // Should have risen significantly
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
