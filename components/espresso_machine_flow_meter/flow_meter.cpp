#include "flow_meter.h"
#include "esphome/core/log.h"

namespace esphome {
namespace espresso_machine_flow_meter {

static const char *const TAG = "espresso_machine_flow_meter";

void FlowMeter::setup() {
  ESP_LOGI(TAG, "Flow meter initialised (%.4f pulses/ml)", pulses_per_ml_);
  pin_->setup();
  pin_->attach_interrupt(FlowMeter::pulse_isr, this, gpio::INTERRUPT_RISING_EDGE);
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

  // Update 3-second rolling average (circular buffer of AVG_WINDOW_SIZE samples)
  avg_buf_[avg_buf_idx_] = rate_;
  avg_buf_idx_ = (avg_buf_idx_ + 1) % AVG_WINDOW_SIZE;
  if (avg_buf_count_ < AVG_WINDOW_SIZE)
    avg_buf_count_++;
  float sum = 0.0f;
  for (uint8_t i = 0; i < avg_buf_count_; i++)
    sum += avg_buf_[i];
  avg_rate_3s_ = (avg_buf_count_ > 0) ? (sum / static_cast<float>(avg_buf_count_)) : 0.0f;

  if (rate_sensor_ != nullptr)
    rate_sensor_->publish_state(rate_);
  if (total_sensor_ != nullptr)
    total_sensor_->publish_state(total_volume_);
  if (avg_rate_sensor_ != nullptr)
    avg_rate_sensor_->publish_state(avg_rate_3s_);
}

void FlowMeter::reset() {
  pulse_count_ = 0;
  last_pulse_count_ = 0;
  total_volume_ = 0.0f;
  rate_ = 0.0f;
  for (uint8_t i = 0; i < AVG_WINDOW_SIZE; i++)
    avg_buf_[i] = 0.0f;
  avg_buf_idx_ = 0;
  avg_buf_count_ = 0;
  avg_rate_3s_ = 0.0f;
  ESP_LOGI(TAG, "Flow meter reset");
}

void FlowMeter::calibrate(float actual_volume_ml) {
  if (actual_volume_ml <= 0.0f || pulse_count_ == 0)
    return;
  pulses_per_ml_ = static_cast<float>(pulse_count_) / actual_volume_ml;
  ESP_LOGI(TAG, "Flow meter calibrated: %.4f pulses/ml", pulses_per_ml_);
}

}  // namespace espresso_machine_flow_meter
}  // namespace esphome
