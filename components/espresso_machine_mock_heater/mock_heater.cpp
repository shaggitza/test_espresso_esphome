#include "mock_heater.h"
#include <algorithm>
#include <cmath>
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
    float temp = parent_->get_sensor_temperature();
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
    case ParamType::DIST_WATER_TO_HEATER:
      initial_value = parent_->get_dist_water_to_heater();
      break;
    case ParamType::DIST_WATER_TO_SENSOR:
      initial_value = parent_->get_dist_water_to_sensor();
      break;
    case ParamType::DIST_SENSOR_TO_HEATER:
      initial_value = parent_->get_dist_sensor_to_heater();
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
    case ParamType::DIST_WATER_TO_HEATER:
      parent_->update_dist_water_to_heater(value);
      ESP_LOGD(TAG, "Thermal distance water-to-heater updated to %.1f mm", value);
      break;
    case ParamType::DIST_WATER_TO_SENSOR:
      parent_->update_dist_water_to_sensor(value);
      ESP_LOGD(TAG, "Thermal distance water-to-sensor updated to %.1f mm", value);
      break;
    case ParamType::DIST_SENSOR_TO_HEATER:
      parent_->update_dist_sensor_to_heater(value);
      ESP_LOGD(TAG, "Thermal distance sensor-to-heater updated to %.1f mm", value);
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
  // Initialize all temperature nodes to the same starting temperature
  sensor_temperature_ = temperature_;
  heater_temperature_ = temperature_;
  last_update_ms_ = millis();
  ESP_LOGI(TAG, "Mock heater initialized:");
  ESP_LOGI(TAG, "  Initial temp: %.1f °C", temperature_);
  ESP_LOGI(TAG, "  Ambient temp: %.1f °C", ambient_temp_);
  ESP_LOGI(TAG, "  Power: %.0f W", power_watts_);
  ESP_LOGI(TAG, "  Thermal mass: %.0f J/°C (Al block + water)", thermal_mass_);
  ESP_LOGI(TAG, "  Heat loss: %.2f W/°C", heat_loss_);
  ESP_LOGI(TAG, "  Water inlet temp: %.1f °C", water_inlet_temp_);
  ESP_LOGI(TAG, "  Heat transfer k: %.2f mL/s (ε≈63%% at Q=k)", heat_transfer_k_);
  ESP_LOGI(TAG, "  Dist water-heater: %.1f mm", dist_water_to_heater_mm_);
  ESP_LOGI(TAG, "  Dist water-sensor: %.1f mm", dist_water_to_sensor_mm_);
  ESP_LOGI(TAG, "  Dist sensor-heater: %.1f mm", dist_sensor_to_heater_mm_);
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

  // Thermal ODE with optional 3-node model
  //
  // Nodes (when all distances > 0):
  //   H — heater element (where electrical energy is deposited), C_H = 10% of C
  //   W — water contact / bulk block (where flow cooling acts),  C_W = 90% of C
  //   S — temperature sensor probe (what PID reads),            C_S = 5% of C
  //
  // Thermal conductances between nodes [W/K]:
  //   G = K_Al × A_ref / d   where K_Al = 200 W/(m·K), A_ref = 1 cm² = 1e-4 m²
  //   G = 0 when d = 0 (no lag; node is coupled directly to its source)
  //
  // Heat flows [W]:
  //   Q_HW = G_HW × (T_H − T_W)  — heater element to block
  //   Q_WS = G_WS × (T_W − T_S)  — block to sensor
  //   Q_HS = G_HS × (T_H − T_S)  — heater element to sensor (direct path)
  //
  // When all distances = 0: G = 0 for all, reverts to original 1-node model.
  static constexpr float CP_WATER = 4.186f;  // J/(mL·°C)
  static constexpr float K_AL = 200.0f;      // W/(m·K) aluminium
  static constexpr float A_REF = 1e-4f;      // m² reference cross-section (~1 cm²)

  // Heat transfer effectiveness for flow-based cooling (unchanged from original)
  float effectiveness = 1.0f;
  if (flow_rate_ > 0.01f) {
    effectiveness = 1.0f - std::exp(-heat_transfer_k_ / flow_rate_);
  }
  float Q_flow = flow_rate_ * CP_WATER * effectiveness * (temperature_ - water_inlet_temp_);
  float Q_loss = heat_loss_ * (temperature_ - ambient_temp_);
  float Q_elec = duty * power_watts_;

  // Thermal conductances from distances [mm] — zero distance means perfect coupling
  float G_HW = (dist_water_to_heater_mm_ > 0.0f)
      ? K_AL * A_REF / (dist_water_to_heater_mm_ * 1e-3f) : 0.0f;
  float G_WS = (dist_water_to_sensor_mm_ > 0.0f)
      ? K_AL * A_REF / (dist_water_to_sensor_mm_ * 1e-3f) : 0.0f;
  float G_HS = (dist_sensor_to_heater_mm_ > 0.0f)
      ? K_AL * A_REF / (dist_sensor_to_heater_mm_ * 1e-3f) : 0.0f;

  float Q_HW = G_HW * (heater_temperature_ - temperature_);
  float Q_WS = G_WS * (temperature_ - sensor_temperature_);
  float Q_HS = G_HS * (heater_temperature_ - sensor_temperature_);

  bool has_heater_node = (G_HW > 0.0f || G_HS > 0.0f);
  bool has_sensor_node = (G_WS > 0.0f || G_HS > 0.0f);

  if (has_heater_node) {
    // 3-node: heater element is thermally separate from the block.
    // C_H = 10% of total thermal mass; C_W = 90% of total thermal mass.
    float C_H = 0.10f * thermal_mass_;
    float C_W = 0.90f * thermal_mass_;
    float dT_H = dt_s * (Q_elec - Q_HW - Q_HS) / C_H;
    float dT_W = dt_s * (Q_HW - Q_loss - Q_flow - Q_WS) / C_W;
    heater_temperature_ += dT_H;
    temperature_ += dT_W;
  } else {
    // Original 1-node model: all electrical heat goes directly to block
    float dT_W = dt_s * (Q_elec - Q_loss - Q_flow - Q_WS) / thermal_mass_;
    temperature_ += dT_W;
    heater_temperature_ = temperature_;
  }

  if (has_sensor_node) {
    // Sensor node: driven by heat flowing from block (Q_WS) and from heater
    // element directly (Q_HS). C_S = 5% of total thermal mass.
    float C_S = 0.05f * thermal_mass_;
    sensor_temperature_ += dt_s * (Q_WS + Q_HS) / C_S;
  } else {
    // No sensor lag: sensor instantly reads block temperature
    sensor_temperature_ = temperature_;
  }

  // Clamp all nodes to physical bounds
  // Floor: 10°C below ambient (can't cool below ambient naturally in this model)
  // Ceiling: 300°C safety cap for simulation (prevents runaway in degenerate cases)
  auto clamp = [this](float &t) {
    if (t < ambient_temp_ - 10.0f) t = ambient_temp_ - 10.0f;
    if (t > 300.0f) t = 300.0f;
  };
  clamp(temperature_);
  clamp(heater_temperature_);
  clamp(sensor_temperature_);

  // Log periodically (every ~5 seconds for debugging)
  static uint32_t last_log_ms = 0;
  if (now - last_log_ms > 5000) {
    last_log_ms = now;
    ESP_LOGD(TAG, "T_block=%.1f°C T_sensor=%.1f°C duty=%.1f%% Q=%.2f mL/s ε=%.0f%%",
             temperature_, sensor_temperature_, duty * 100.0f, flow_rate_,
             effectiveness * 100.0f);
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
