#include "heater.h"
#include "esphome/core/log.h"

namespace esphome {
namespace espresso_machine_heater {

static const char *const TAG = "espresso_machine_heater";

void SsrPeriodNumber::control(float value) {
  if (parent_ != nullptr)
    parent_->set_ssr_period_ms(static_cast<uint32_t>(value));
  publish_state(value);
  ESP_LOGI(TAG, "SSR period updated to %.0f ms", value);
}

}  // namespace espresso_machine_heater
}  // namespace esphome
