#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/output/float_output.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/number/number.h"
#include "../espresso_machine/interfaces.h"

namespace esphome {
namespace espresso_machine_mock_heater {

class MockHeater;

// ---------------------------------------------------------------------------
// MockHeaterOutput — float output that PID's heat_output references
// Receives duty cycle 0.0–1.0 from PID and stores it for the thermal ODE.
// ---------------------------------------------------------------------------
class MockHeaterOutput : public output::FloatOutput, public Component {
 public:
  void set_parent(MockHeater *parent) { parent_ = parent; }

  void setup() override {}
  void loop() override {}

  float get_duty() const { return duty_; }

 protected:
  void write_state(float state) override;

  MockHeater *parent_{nullptr};
  float duty_{0.0f};
};

// ---------------------------------------------------------------------------
// MockHeaterTempSensor — temperature sensor that PID's sensor references
// Polls the simulated temperature from MockHeater and publishes it.
// ---------------------------------------------------------------------------
class MockHeaterTempSensor : public sensor::Sensor, public PollingComponent {
 public:
  void set_parent(MockHeater *parent) { parent_ = parent; }

  void setup() override {}
  void update() override;

 protected:
  MockHeater *parent_{nullptr};
};

// ---------------------------------------------------------------------------
// MockHeaterNumber — number entity for runtime tuning of physics parameters
// ---------------------------------------------------------------------------
class MockHeaterNumber : public number::Number, public Component {
 public:
  enum class ParamType {
    POWER,
    THERMAL_MASS,
    HEAT_LOSS,
    AMBIENT,
    HEAT_TRANSFER_K,
    DIST_WATER_TO_HEATER,
    DIST_WATER_TO_SENSOR,
    DIST_SENSOR_TO_HEATER,
  };

  void set_parent(MockHeater *parent) { parent_ = parent; }
  void set_param_type(ParamType type) { param_type_ = type; }

  void setup() override;
  void loop() override {}

 protected:
  void control(float value) override;

  MockHeater *parent_{nullptr};
  ParamType param_type_{ParamType::POWER};
};

// ---------------------------------------------------------------------------
// MockHeater — main component managing thermal simulation
// ---------------------------------------------------------------------------
class MockHeater : public Component,
                   public espresso_machine::IFlowObserver,
                   public espresso_machine::IHeater {
 public:
  // Configuration setters (called from generated code)
  void set_initial_temperature(float t) { temperature_ = t; }
  void set_ambient_temperature(float t) { ambient_temp_ = t; }
  void set_power_watts(float p) { power_watts_ = p; }
  void set_thermal_mass(float c) { thermal_mass_ = c; }
  void set_heat_loss(float h) { heat_loss_ = h; }
  void set_water_inlet_temp(float t) { water_inlet_temp_ = t; }
  void set_dist_water_to_heater(float d) { dist_water_to_heater_mm_ = d; }
  void set_dist_water_to_sensor(float d) { dist_water_to_sensor_mm_ = d; }
  void set_dist_sensor_to_heater(float d) { dist_sensor_to_heater_mm_ = d; }

  // Called by MockPump each loop tick to drive flow-based cooling (IFlowObserver)
  void set_flow_rate(float flow_rate_ml_s) override { flow_rate_ = flow_rate_ml_s; }

  // IHeater — temperature reading and setpoint commanding
  float get_current_temperature() const override { return sensor_temperature_; }
  void set_target_temperature(float t) override;

  void set_output(MockHeaterOutput *out) { output_ = out; }
  void set_temperature_sensor(MockHeaterTempSensor *sens) { temp_sensor_ = sens; }
  void set_duty_sensor(sensor::Sensor *s) { duty_sensor_ = s; }

  // Runtime tuning number entities
  void set_power_number(MockHeaterNumber *num) {
    power_number_ = num;
    if (num) num->set_param_type(MockHeaterNumber::ParamType::POWER);
  }
  void set_thermal_mass_number(MockHeaterNumber *num) {
    thermal_mass_number_ = num;
    if (num) num->set_param_type(MockHeaterNumber::ParamType::THERMAL_MASS);
  }
  void set_heat_loss_number(MockHeaterNumber *num) {
    heat_loss_number_ = num;
    if (num) num->set_param_type(MockHeaterNumber::ParamType::HEAT_LOSS);
  }
  void set_ambient_number(MockHeaterNumber *num) {
    ambient_number_ = num;
    if (num) num->set_param_type(MockHeaterNumber::ParamType::AMBIENT);
  }
  void set_heat_transfer_k_number(MockHeaterNumber *num) {
    heat_transfer_k_number_ = num;
    if (num) num->set_param_type(MockHeaterNumber::ParamType::HEAT_TRANSFER_K);
  }
  void set_dist_water_to_heater_number(MockHeaterNumber *num) {
    dist_water_to_heater_number_ = num;
    if (num) num->set_param_type(MockHeaterNumber::ParamType::DIST_WATER_TO_HEATER);
  }
  void set_dist_water_to_sensor_number(MockHeaterNumber *num) {
    dist_water_to_sensor_number_ = num;
    if (num) num->set_param_type(MockHeaterNumber::ParamType::DIST_WATER_TO_SENSOR);
  }
  void set_dist_sensor_to_heater_number(MockHeaterNumber *num) {
    dist_sensor_to_heater_number_ = num;
    if (num) num->set_param_type(MockHeaterNumber::ParamType::DIST_SENSOR_TO_HEATER);
  }

  void setup() override;
  void loop() override;

  // Accessors for sub-entities
  float get_temperature() const { return temperature_; }
  float get_sensor_temperature() const { return sensor_temperature_; }
  float get_heater_temperature() const { return heater_temperature_; }
  float get_duty() const { return output_ ? output_->get_duty() : 0.0f; }

  // Runtime parameter accessors/mutators
  float get_power_watts() const { return power_watts_; }
  float get_thermal_mass() const { return thermal_mass_; }
  float get_heat_loss() const { return heat_loss_; }
  float get_ambient_temp() const { return ambient_temp_; }
  float get_water_inlet_temp() const { return water_inlet_temp_; }
  float get_flow_rate() const { return flow_rate_; }
  float get_heat_transfer_k() const { return heat_transfer_k_; }
  float get_dist_water_to_heater() const { return dist_water_to_heater_mm_; }
  float get_dist_water_to_sensor() const { return dist_water_to_sensor_mm_; }
  float get_dist_sensor_to_heater() const { return dist_sensor_to_heater_mm_; }

  void update_power_watts(float v) { power_watts_ = v; }
  void update_thermal_mass(float v) { thermal_mass_ = v; }
  void update_heat_loss(float v) { heat_loss_ = v; }
  void update_ambient_temp(float v) { ambient_temp_ = v; }
  void update_heat_transfer_k(float v) { heat_transfer_k_ = v; }
  void set_heat_transfer_k(float k) { heat_transfer_k_ = k; }
  void update_dist_water_to_heater(float v) { dist_water_to_heater_mm_ = v; }
  void update_dist_water_to_sensor(float v) { dist_water_to_sensor_mm_ = v; }
  void update_dist_sensor_to_heater(float v) { dist_sensor_to_heater_mm_ = v; }

 protected:
  // Physics parameters
  // Default: 800g Al × 0.897 J/(g·°C) + 20mL water × 4.186 J/(mL·°C) ≈ 800 J/°C
  float temperature_{25.0f};       // Current simulated water-contact block temperature [°C]
  float ambient_temp_{25.0f};      // Ambient temperature [°C]
  float power_watts_{1200.0f};     // Heater power [W]
  float thermal_mass_{800.0f};     // Thermal mass [J/°C] (Al block + water)
  float heat_loss_{1.7f};          // Heat-loss coefficient [W/°C]
  float water_inlet_temp_{20.0f};  // Cold-water inlet temperature [°C]
  // Heat transfer effectiveness constant [mL/s].
  // At flow_rate = heat_transfer_k, effectiveness ≈ 63%.
  // At higher flows, effectiveness drops (less time in contact).
  // At lower flows, effectiveness approaches 100%.
  // Typical thermoblock: 1.5–3.0 mL/s.
  float heat_transfer_k_{2.0f};

  // Thermal distance parameters [mm] — aluminium thermoblock geometry.
  // Each non-zero distance creates a separate thermal node connected through
  // aluminium (K_Al=200 W/(m·K), A_ref=1 cm²), introducing a lag that makes
  // PID control harder and more realistic.
  // Set to 0 (default) to disable the corresponding lag (backward compatible).
  float dist_water_to_heater_mm_{0.0f};  // Heater element → water contact
  float dist_water_to_sensor_mm_{0.0f};  // Water contact → sensor probe
  float dist_sensor_to_heater_mm_{0.0f}; // Heater element → sensor probe (direct path)

  // Additional temperature nodes for the 3-node thermal model.
  // When all distances = 0, these equal temperature_ (original 1-node model).
  float sensor_temperature_{25.0f};   // Temperature at sensor probe (what PID reads)
  float heater_temperature_{25.0f};   // Temperature at heater element

  // Target temperature commanded via IHeater::set_target_temperature()
  // Used for tracking/logging; actual control driven by PID output.
  float target_temperature_{0.0f};

  // Flow rate pushed by MockPump each tick [mL/s]
  float flow_rate_{0.0f};

  // Sub-entities
  MockHeaterOutput *output_{nullptr};
  MockHeaterTempSensor *temp_sensor_{nullptr};
  sensor::Sensor *duty_sensor_{nullptr};

  // Runtime tuning numbers
  MockHeaterNumber *power_number_{nullptr};
  MockHeaterNumber *thermal_mass_number_{nullptr};
  MockHeaterNumber *heat_loss_number_{nullptr};
  MockHeaterNumber *ambient_number_{nullptr};
  MockHeaterNumber *heat_transfer_k_number_{nullptr};
  MockHeaterNumber *dist_water_to_heater_number_{nullptr};
  MockHeaterNumber *dist_water_to_sensor_number_{nullptr};
  MockHeaterNumber *dist_sensor_to_heater_number_{nullptr};

  // Timing for ODE integration
  uint32_t last_update_ms_{0};
};

}  // namespace espresso_machine_mock_heater
}  // namespace esphome
