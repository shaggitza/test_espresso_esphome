#include "mock_pump.h"
#include "esphome/core/hal.h"
#include <cmath>

namespace esphome {
namespace espresso_machine_mock_pump {

static const char *const TAG = "mock_pump";

// ---------------------------------------------------------------------------
// MockPumpNumber
// ---------------------------------------------------------------------------
void MockPumpNumber::setup() {
  if (!parent_) return;

  // Publish the initial value from the parent
  float initial_value = 0.0f;
  switch (param_type_) {
    case ParamType::NOMINAL_FLOW:
      initial_value = parent_->get_nominal_flow();
      break;
    case ParamType::PUCK_TIME_CONSTANT:
      initial_value = parent_->get_puck_time_constant();
      break;
  }
  this->publish_state(initial_value);
}

void MockPumpNumber::control(float value) {
  if (!parent_) return;

  switch (param_type_) {
    case ParamType::NOMINAL_FLOW:
      parent_->update_nominal_flow(value);
      ESP_LOGD(TAG, "Nominal flow updated to %.1f mL/s", value);
      break;
    case ParamType::PUCK_TIME_CONSTANT:
      parent_->update_puck_time_constant(value);
      ESP_LOGD(TAG, "Puck time constant updated to %.1f s", value);
      break;
  }
  this->publish_state(value);
}

// ---------------------------------------------------------------------------
// MockPump
// ---------------------------------------------------------------------------
void MockPump::setup() {
  last_update_ms_ = millis();
  ESP_LOGI(TAG, "Mock pump initialized:");
  ESP_LOGI(TAG, "  Nominal flow: %.1f mL/s", nominal_flow_);
  ESP_LOGI(TAG, "  Puck time constant: %.1f s", puck_time_constant_);
}

void MockPump::loop() {
  uint32_t now = millis();
  uint32_t dt_ms = now - last_update_ms_;

  // Update every ~10 ms for smooth simulation (same rate as mock heater)
  if (dt_ms < 10) {
    return;
  }
  last_update_ms_ = now;

  float dt_s = dt_ms / 1000.0f;

  if (running_) {
    run_time_ += dt_s;

    // Puck wetting model: Q(t) = Q_nom × (1 − exp(−t/τ))
    // Flow starts near zero and ramps exponentially to nominal
    if (puck_time_constant_ > 0.0f) {
      current_flow_rate_ = nominal_flow_ * (1.0f - std::exp(-run_time_ / puck_time_constant_));
    } else {
      current_flow_rate_ = nominal_flow_;
    }

    // Accumulate volume: V += Q × dt
    total_volume_ += current_flow_rate_ * dt_s;

    // Update sensors periodically (~4 Hz is fine for display)
    static uint32_t last_sensor_publish_ms = 0;
    if (now - last_sensor_publish_ms > 250) {
      last_sensor_publish_ms = now;
      if (rate_sensor_) {
        rate_sensor_->publish_state(current_flow_rate_);
      }
      if (total_sensor_) {
        total_sensor_->publish_state(total_volume_);
      }
    }
  } else {
    // Pump is off — flow decays to zero
    if (current_flow_rate_ > 0.0f) {
      // Quick decay when pump stops
      current_flow_rate_ *= 0.9f;
      if (current_flow_rate_ < 0.01f) {
        current_flow_rate_ = 0.0f;
        if (rate_sensor_) {
          rate_sensor_->publish_state(0.0f);
        }
      }
    }
    run_time_ = 0.0f;  // Reset run time for next start
  }

  // Push current flow rate to mock heater for thermoblock cooling simulation
  if (flow_observer_) {
    flow_observer_->set_flow_rate(current_flow_rate_);
  }

  // Log periodically (every ~5 seconds for debugging)
  static uint32_t last_log_ms = 0;
  if (now - last_log_ms > 5000) {
    last_log_ms = now;
    ESP_LOGD(TAG, "Pump %s, Q=%.2f mL/s, V=%.1f mL, t=%.1f s",
             running_ ? "ON" : "OFF", current_flow_rate_, total_volume_, run_time_);
  }
}

void MockPump::write_state(bool state) {
  running_ = state;
  this->publish_state(state);

  if (state) {
    // Pump starting — reset run time, but NOT total volume
    // (total volume is only reset by explicit reset_flow() call)
    run_time_ = 0.0f;
    ESP_LOGI(TAG, "Mock pump started");
  } else {
    ESP_LOGI(TAG, "Mock pump stopped (total: %.1f mL)", total_volume_);
  }
}

void MockPump::reset_flow() {
  total_volume_ = 0.0f;
  current_flow_rate_ = 0.0f;
  run_time_ = 0.0f;

  if (rate_sensor_) {
    rate_sensor_->publish_state(0.0f);
  }
  if (total_sensor_) {
    total_sensor_->publish_state(0.0f);
  }

  ESP_LOGD(TAG, "Flow counters reset");
}

}  // namespace espresso_machine_mock_pump
}  // namespace esphome
