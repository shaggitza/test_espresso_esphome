#include "mock_pump.h"
#include "esphome/core/log.h"

namespace esphome {
namespace espresso_machine_mock_pump {

static const char *const TAG = "espresso_machine_mock_pump";

// ---------------------------------------------------------------------------
// MockPumpParamNumber
// ---------------------------------------------------------------------------

void MockPumpParamNumber::control(float value) {
  if (parent_ != nullptr) {
    switch (param_) {
      case MockPumpParam::NOMINAL_FLOW:
        parent_->set_nominal_flow_runtime(value);
        break;
      case MockPumpParam::PUCK_TAU:
        parent_->set_puck_tau_runtime(value);
        break;
    }
  }
  publish_state(value);
  ESP_LOGI(TAG, "Param updated: %.2f", value);
}

// ---------------------------------------------------------------------------
// MockPump
// ---------------------------------------------------------------------------

void MockPump::setup() {
  ESP_LOGI(TAG, "Mock pump init: Q_nom=%.2fml/s tau=%.1fs", nominal_flow_, puck_tau_);
  last_update_ms_ = millis();

  if (nominal_flow_number_ != nullptr)
    nominal_flow_number_->publish_state(nominal_flow_);
  if (puck_tau_number_ != nullptr)
    puck_tau_number_->publish_state(puck_tau_);
}

void MockPump::loop() {
  if (!running_)
    return;

  uint32_t now = millis();
  uint32_t elapsed_ms = now - last_update_ms_;
  if (elapsed_ms < 50)
    return;

  last_update_ms_ = now;
  float dt_s = static_cast<float>(elapsed_ms) / 1000.0f;

  // Advance cumulative on-time and compute instantaneous flow rate.
  // Q(t) = nominal × (1 − exp(−t / tau))
  time_on_s_ += dt_s;
  flow_rate_ = nominal_flow_ * (1.0f - expf(-time_on_s_ / puck_tau_));
  flow_total_ += flow_rate_ * dt_s;

  ESP_LOGD(TAG, "Q=%.2fml/s V=%.1fml t_on=%.1fs", flow_rate_, flow_total_, time_on_s_);
}

void MockPump::turn_on() {
  if (running_)
    return;
  ESP_LOGI(TAG, "Mock pump ON");
  running_ = true;
  last_update_ms_ = millis();
}

void MockPump::turn_off() {
  if (!running_)
    return;
  ESP_LOGI(TAG, "Mock pump OFF (Q=%.2fml/s V=%.1fml)", flow_rate_, flow_total_);
  running_ = false;
  flow_rate_ = 0.0f;
}

void MockPump::reset_flow() {
  ESP_LOGI(TAG, "Mock pump flow reset");
  flow_rate_ = 0.0f;
  flow_total_ = 0.0f;
  time_on_s_ = 0.0f;
  last_update_ms_ = millis();
}

}  // namespace espresso_machine_mock_pump
}  // namespace esphome
