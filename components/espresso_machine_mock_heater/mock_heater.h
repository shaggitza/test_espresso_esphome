#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/output/output.h"

namespace esphome {
namespace espresso_machine_mock_heater {

// Forward declaration so MockHeaterParamNumber can reference MockHeater.
class MockHeater;

// ---------------------------------------------------------------------------
// MockHeaterParam — identifies which thermal parameter a number entity controls
// ---------------------------------------------------------------------------
enum class MockHeaterParam : uint8_t {
  POWER_WATTS = 0,
  THERMAL_MASS = 1,
  HEAT_LOSS = 2,
  AMBIENT_TEMP = 3,
};

// ---------------------------------------------------------------------------
// MockHeaterParamNumber — number entity that updates a single thermal parameter
//
// One instance is created per HA-adjustable parameter (power_watts,
// thermal_mass_j_per_c, heat_loss_w_per_c, ambient_temp).  When HA writes a
// new value the control() override forwards it to the correct setter on the
// parent MockHeater via the param_ discriminator.
// ---------------------------------------------------------------------------
class MockHeaterParamNumber : public number::Number {
 public:
  MockHeaterParamNumber() = default;
  void set_parent(MockHeater *parent, MockHeaterParam param) {
    parent_ = parent;
    param_ = param;
  }

 protected:
  void control(float value) override;

  MockHeater *parent_{nullptr};
  MockHeaterParam param_{MockHeaterParam::POWER_WATTS};
};

// ---------------------------------------------------------------------------
// MockHeater — physics-based thermal simulator
//
// Sits at the physical layer so the PID climate cannot distinguish it from a
// real thermocouple + SSR combination:
//   • Extends output::FloatOutput so the PID writes its duty cycle (0–1) to
//     write_state(), exactly as it drives a slow_pwm SSR output.
//   • Owns a sensor::Sensor* (temperature_sensor) that the PID reads from,
//     exactly as it reads from a MAX31855 thermocouple.
//
// Thermal model (integrated every 100 ms in loop()):
//   dT/dt = (duty × power_w − heat_loss × (T − ambient)) / thermal_mass
//
// Steady-state temperature at constant duty:
//   T_ss = ambient + duty × power_w / heat_loss_w_per_c
// ---------------------------------------------------------------------------
class MockHeater : public output::FloatOutput, public Component {
 public:
  // ----- Static configuration setters (called from Python codegen) ----------
  void set_initial_temp(float t) { temp_ = t; }
  void set_power_watts(float p) { power_w_ = p; }
  void set_thermal_mass_j_per_c(float m) { thermal_mass_ = m; }
  void set_heat_loss_w_per_c(float l) { heat_loss_ = l; }
  void set_ambient_temp(float a) { ambient_temp_ = a; }

  // ----- Child entity setters -----------------------------------------------
  void set_temperature_sensor(sensor::Sensor *s) { temp_sensor_ = s; }
  void set_power_number(MockHeaterParamNumber *n) { power_number_ = n; }
  void set_thermal_mass_number(MockHeaterParamNumber *n) { thermal_mass_number_ = n; }
  void set_heat_loss_number(MockHeaterParamNumber *n) { heat_loss_number_ = n; }
  void set_ambient_temp_number(MockHeaterParamNumber *n) { ambient_temp_number_ = n; }

  // ----- Runtime parameter updates (called by MockHeaterParamNumber) --------
  void set_power_watts_runtime(float p) {
    if (p > 0.0f)
      power_w_ = p;
  }
  void set_thermal_mass_runtime(float m) {
    if (m > 0.0f)
      thermal_mass_ = m;
  }
  void set_heat_loss_runtime(float l) {
    if (l > 0.0f)
      heat_loss_ = l;
  }
  void set_ambient_temp_runtime(float a) { ambient_temp_ = a; }

  // ----- ESPHome lifecycle --------------------------------------------------
  void setup() override;
  void loop() override;

  // ----- Inspection (used in unit tests) ------------------------------------
  float get_current_temp() const { return temp_; }
  float get_duty() const { return duty_; }

 protected:
  // output::FloatOutput interface — PID writes duty cycle here
  void write_state(float duty) override { duty_ = duty; }

  // -- Simulation state ------------------------------------------------------
  float temp_{20.0f};      // current simulated temperature (°C)
  float duty_{0.0f};       // current heater duty cycle (0–1)

  // -- Thermal parameters (may be updated at runtime via number entities) ----
  float power_w_{1200.0f};     // heater rated power (W)
  float thermal_mass_{1256.0f};  // thermoblock thermal mass (J/°C)
  float heat_loss_{1.7f};      // heat loss coefficient (W/°C above ambient)
  float ambient_temp_{20.0f};  // room temperature (°C)

  uint32_t last_update_ms_{0};

  // -- Child entities --------------------------------------------------------
  sensor::Sensor *temp_sensor_{nullptr};
  MockHeaterParamNumber *power_number_{nullptr};
  MockHeaterParamNumber *thermal_mass_number_{nullptr};
  MockHeaterParamNumber *heat_loss_number_{nullptr};
  MockHeaterParamNumber *ambient_temp_number_{nullptr};
};

}  // namespace espresso_machine_mock_heater
}  // namespace esphome
