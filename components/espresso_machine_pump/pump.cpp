#include "pump.h"
#include "esphome/core/log.h"

namespace esphome {
namespace espresso_machine_pump {

static const char *const TAG = "espresso_machine_pump";

void Pump::setup() {
  ESP_LOGI(TAG, "Pump initialised (type=%s)", type_ == PumpType::RELAY ? "relay" : "dimmer");
  pin_->setup();
  pin_->digital_write(false);
}

void Pump::turn_on() {
  if (running_)
    return;
  ESP_LOGI(TAG, "Pump ON");
  running_ = true;
  pin_->digital_write(true);
}

void Pump::turn_off() {
  if (!running_)
    return;
  ESP_LOGI(TAG, "Pump OFF");
  running_ = false;
  pin_->digital_write(false);
}

}  // namespace espresso_machine_pump
}  // namespace esphome
