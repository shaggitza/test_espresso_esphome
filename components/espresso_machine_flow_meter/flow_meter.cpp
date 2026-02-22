#include "flow_meter.h"
#include "esphome/core/log.h"

namespace esphome {
namespace espresso_machine_flow_meter {

static const char *const TAG = "espresso_machine_flow_meter";

void FlowMeter::setup() {
  ESP_LOGI(TAG, "Flow meter initialised (%.4f pulses/ml)", pulses_per_ml_);
  pin_->setup();
  last_update_ms_ = millis();
}

void FlowMeter::loop() {
  uint32_t now = millis();
  uint32_t elapsed_ms = now - last_update_ms_;
  if (elapsed_ms < 100)
    return;

  uint32_t count = pulse_count_;
  uint32_t delta = count - last_pulse_count_;
  last_pulse_count_ = count;
  last_update_ms_ = now;

  if (pulses_per_ml_ > 0.0f) {
    float delta_ml = static_cast<float>(delta) / pulses_per_ml_;
    total_volume_ += delta_ml;
    rate_ = delta_ml / (static_cast<float>(elapsed_ms) / 1000.0f);
  }
}

void FlowMeter::reset() {
  pulse_count_ = 0;
  last_pulse_count_ = 0;
  total_volume_ = 0.0f;
  rate_ = 0.0f;
  ESP_LOGI(TAG, "Flow meter reset");
}

}  // namespace espresso_machine_flow_meter
}  // namespace esphome
