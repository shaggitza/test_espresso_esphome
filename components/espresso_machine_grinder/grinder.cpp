#include "grinder.h"
#include "esphome/core/log.h"

namespace esphome {
namespace espresso_machine_grinder {

static const char *const TAG = "espresso_machine_grinder";

// ---------------------------------------------------------------------------
// GrinderTimeNumber
// ---------------------------------------------------------------------------

void GrinderTimeNumber::control(float value) {
  parent_->set_default_grind_time(static_cast<uint32_t>(value));
  publish_state(value);
  ESP_LOGI(TAG, "Default grind time updated to %.0f ms", value);
}

// ---------------------------------------------------------------------------
// Grinder
// ---------------------------------------------------------------------------

void Grinder::setup() {
  ESP_LOGI(TAG, "Grinder initialised (type=%s, default_grind_time=%ums)",
           type_ == GrinderType::RELAY ? "relay" : "none", default_grind_time_ms_);
  if (pin_ != nullptr) {
    pin_->setup();
    pin_->digital_write(false);
  }
  if (grind_time_number_ != nullptr)
    grind_time_number_->publish_state(static_cast<float>(default_grind_time_ms_));
}

void Grinder::loop() {
  if (!grinding_)
    return;
  // Use subtraction for rollover-safe comparison (~49-day millis() wraparound)
  if ((int32_t)(millis() - grind_end_ms_) >= 0) {
    stop();
  }
}

void Grinder::grind(uint32_t duration_ms) {
  if (orchestrator_ != nullptr && orchestrator_->is_busy()) {
    ESP_LOGW(TAG, "Grind blocked: machine is busy (brewing or steaming)");
    return;
  }
  if (type_ == GrinderType::NONE || grinding_)
    return;
  uint32_t ms = duration_ms > 0 ? duration_ms : default_grind_time_ms_;
  ESP_LOGI(TAG, "Starting grind for %ums", ms);
  grinding_ = true;
  grind_end_ms_ = millis() + ms;
  if (pin_ != nullptr)
    pin_->digital_write(true);
}

void Grinder::stop() {
  if (!grinding_)
    return;
  ESP_LOGI(TAG, "Grind complete");
  grinding_ = false;
  if (pin_ != nullptr)
    pin_->digital_write(false);
}

}  // namespace espresso_machine_grinder
}  // namespace esphome
