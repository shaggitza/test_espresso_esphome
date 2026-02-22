#include "mock_heater.h"
#include "esphome/core/log.h"

namespace esphome {
namespace espresso_machine_mock_heater {

static const char *const TAG = "espresso_machine_mock_heater";

// ---------------------------------------------------------------------------
// MockHeaterParamNumber
// ---------------------------------------------------------------------------

void MockHeaterParamNumber::control(float value) {
  if (parent_ != nullptr) {
    switch (param_) {
      case MockHeaterParam::POWER_WATTS:
        parent_->set_power_watts_runtime(value);
        break;
      case MockHeaterParam::THERMAL_MASS:
        parent_->set_thermal_mass_runtime(value);
        break;
      case MockHeaterParam::HEAT_LOSS:
        parent_->set_heat_loss_runtime(value);
        break;
      case MockHeaterParam::AMBIENT_TEMP:
        parent_->set_ambient_temp_runtime(value);
        break;
    }
  }
  publish_state(value);
  ESP_LOGI(TAG, "Param updated: %.2f", value);
}

// ---------------------------------------------------------------------------
// MockHeater
// ---------------------------------------------------------------------------

void MockHeater::setup() {
  ESP_LOGI(TAG,
           "Mock heater init: T=%.1f°C P=%.0fW C=%.0fJ/°C h=%.2fW/°C Tamb=%.1f°C",
           temp_, power_w_, thermal_mass_, heat_loss_, ambient_temp_);
  last_update_ms_ = millis();

  // Publish initial values to HA number entities so sliders show correct state.
  if (power_number_ != nullptr)
    power_number_->publish_state(power_w_);
  if (thermal_mass_number_ != nullptr)
    thermal_mass_number_->publish_state(thermal_mass_);
  if (heat_loss_number_ != nullptr)
    heat_loss_number_->publish_state(heat_loss_);
  if (ambient_temp_number_ != nullptr)
    ambient_temp_number_->publish_state(ambient_temp_);

  // Publish initial temperature to the sensor so the PID has a starting point.
  if (temp_sensor_ != nullptr)
    temp_sensor_->publish_state(temp_);
}

void MockHeater::loop() {
  uint32_t now = millis();
  uint32_t elapsed_ms = now - last_update_ms_;
  if (elapsed_ms < 100)
    return;

  last_update_ms_ = now;
  float dt_s = static_cast<float>(elapsed_ms) / 1000.0f;

  // Thermal ODE: dT/dt = (duty × P_in − h × (T − T_amb)) / C
  // P_in = duty × power_w  (watts delivered to the thermoblock)
  // heat_out = heat_loss × (T − ambient)  (Newton's law of cooling)
  if (thermal_mass_ > 0.0f) {
    float heat_in = duty_ * power_w_;
    float heat_out = heat_loss_ * (temp_ - ambient_temp_);
    temp_ += (heat_in - heat_out) * dt_s / thermal_mass_;
  }

  if (temp_sensor_ != nullptr)
    temp_sensor_->publish_state(temp_);

  ESP_LOGD(TAG, "T=%.2f°C duty=%.3f", temp_, duty_);
}

}  // namespace espresso_machine_mock_heater
}  // namespace esphome
