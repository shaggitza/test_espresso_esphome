#include "mock_heater.h"
#include <algorithm>
#include "esphome/core/hal.h"

namespace esphome {
namespace espresso_machine_mock_heater {

static const char *const TAG = "mock_heater";

// ---------------------------------------------------------------------------
// MockHeaterOutput
// ---------------------------------------------------------------------------
void MockHeaterOutput::write_state(float state) {
  // Clamp duty cycle to [0, 1]
  duty_ = std::max(0.0f, std::min(1.0f, state));
}

// ---------------------------------------------------------------------------
// MockHeaterTempSensor
// ---------------------------------------------------------------------------
void MockHeaterTempSensor::update() {
  if (parent_) {
    float temp = parent_->get_temperature();
    this->publish_state(temp);
  }
}

// ---------------------------------------------------------------------------
// MockHeaterNumber
// ---------------------------------------------------------------------------
void MockHeaterNumber::setup() {
  if (!parent_) return;

  // Publish the initial value from the parent
  float initial_value = 0.0f;
  switch (param_type_) {
    case ParamType::POWER:
      initial_value = parent_->get_power_watts();
      break;
    case ParamType::THERMAL_MASS:
      initial_value = parent_->get_thermal_mass();
      break;
    case ParamType::HEAT_LOSS:
      initial_value = parent_->get_heat_loss();
      break;
    case ParamType::AMBIENT:
      initial_value = parent_->get_ambient_temp();
      break;
    case ParamType::HEAT_TRANSFER_K:
      initial_value = parent_->get_heat_transfer_k();
      break;
  }
  this->publish_state(initial_value);
}

void MockHeaterNumber::control(float value) {
  if (!parent_) return;

  switch (param_type_) {
    case ParamType::POWER:
      parent_->update_power_watts(value);
      ESP_LOGD(TAG, "Power updated to %.1f W", value);
      break;
    case ParamType::THERMAL_MASS:
      parent_->update_thermal_mass(value);
      ESP_LOGD(TAG, "Thermal mass updated to %.1f J/°C", value);
      break;
    case ParamType::HEAT_LOSS:
      parent_->update_heat_loss(value);
      ESP_LOGD(TAG, "Heat loss updated to %.2f W/°C", value);
      break;
    case ParamType::AMBIENT:
      parent_->update_ambient_temp(value);
      ESP_LOGD(TAG, "Ambient temperature updated to %.1f °C", value);
      break;
    case ParamType::HEAT_TRANSFER_K:
      parent_->update_heat_transfer_k(value);
      ESP_LOGD(TAG, "Heat transfer k updated to %.2f mL/s", value);
      break;
  }
  this->publish_state(value);
}

// ---------------------------------------------------------------------------
// MockHeater
// ---------------------------------------------------------------------------
void MockHeater::set_target_temperature(float t) {
  target_temperature_ = t;
  ESP_LOGD(TAG, "IHeater::set_target_temperature(%.1f°C) — stored (use espresso_machine_heater to control PID)", t);
}

void MockHeater::setup() {
  last_update_ms_ = millis();
  ESP_LOGI(TAG, "Mock heater initialized:");
  ESP_LOGI(TAG, "  Initial temp: %.1f °C", temperature_);
  ESP_LOGI(TAG, "  Ambient temp: %.1f °C", ambient_temp_);
  ESP_LOGI(TAG, "  Power: %.0f W", power_watts_);
  ESP_LOGI(TAG, "  Thermal mass: %.0f J/°C (Al block + water)", thermal_mass_);
  ESP_LOGI(TAG, "  Heat loss: %.2f W/°C", heat_loss_);
  ESP_LOGI(TAG, "  Water inlet temp: %.1f °C", water_inlet_temp_);
  ESP_LOGI(TAG, "  Heat transfer k: %.2f mL/s (ε≈63%% at Q=k)", heat_transfer_k_);
}

void MockHeater::loop() {
  uint32_t now = millis();
  uint32_t dt_ms = now - last_update_ms_;

  // Update every ~10 ms for smooth simulation
  if (dt_ms < 10) {
    return;
  }
  last_update_ms_ = now;

  float dt_s = dt_ms / 1000.0f;

  // Get current duty cycle from PID output
  float duty = output_ ? output_->get_duty() : 0.0f;

  // Thermal ODE: dT/dt = (duty × P − h × (T − T_amb) − Q_effective) / C
  //
  // Heat transfer effectiveness model:
  // At low flow rates water has more contact time, so it absorbs nearly all
  // available heat (effectiveness → 1). At high flow rates water passes through
  // quickly and can't absorb as much (effectiveness → 0).
  //   ε = 1 - exp(-k / Q)   where k = heat_transfer_k_ [mL/s]
  //
  // This models real-world observations that:
  //   - Slow flow cools the block efficiently (water exits hot)
  //   - Fast flow cools less efficiently per mL (water exits warm)
  //
  // Heat removed by water: Q_water = Q × Cp × ε × (T_block - T_inlet)
  static constexpr float CP_WATER = 4.186f;  // J/(mL·°C)

  float heat_in = duty * power_watts_;
  float heat_loss = heat_loss_ * (temperature_ - ambient_temp_);

  // Calculate heat transfer effectiveness based on flow rate
  float effectiveness = 1.0f;
  if (flow_rate_ > 0.01f) {
    effectiveness = 1.0f - std::exp(-heat_transfer_k_ / flow_rate_);
  }
  float heat_flow = flow_rate_ * CP_WATER * effectiveness * (temperature_ - water_inlet_temp_);

  float dT_dt = (heat_in - heat_loss - heat_flow) / thermal_mass_;

  temperature_ += dT_dt * dt_s;

  // Clamp to reasonable physical bounds
  if (temperature_ < ambient_temp_ - 10.0f) {
    temperature_ = ambient_temp_ - 10.0f;  // Can't go much below ambient
  }
  if (temperature_ > 300.0f) {
    temperature_ = 300.0f;  // Safety cap for simulation
  }

  // Log periodically (every ~5 seconds for debugging)
  static uint32_t last_log_ms = 0;
  if (now - last_log_ms > 5000) {
    last_log_ms = now;
    float effectiveness = (flow_rate_ > 0.01f) ? (1.0f - std::exp(-heat_transfer_k_ / flow_rate_)) : 1.0f;
    ESP_LOGD(TAG, "T=%.1f°C, duty=%.1f%%, Q=%.2f mL/s, ε=%.0f%%, dT/dt=%.2f°C/s",
             temperature_, duty * 100.0f, flow_rate_, effectiveness * 100.0f, dT_dt);
  }

  // Publish duty cycle sensor at ~4 Hz so HA can show SSR switching intensity
  static uint32_t last_duty_publish_ms = 0;
  if (now - last_duty_publish_ms > 250) {
    last_duty_publish_ms = now;
    if (duty_sensor_) {
      duty_sensor_->publish_state(duty * 100.0f);
    }
  }
}

}  // namespace espresso_machine_mock_heater
}  // namespace esphome
