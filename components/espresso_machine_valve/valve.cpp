#include "valve.h"
#include "esphome/core/log.h"

namespace esphome {
namespace espresso_machine_valve {

static const char *const TAG = "espresso_machine_valve";

void Valve::setup() {
  ESP_LOGI(TAG, "Valve initialised (normally_open=%s)", normally_open_ ? "true" : "false");
  pin_->setup();
  // Safe default: ensure valve is closed on boot
  write_pin_(false);
}

void Valve::open() {
  if (is_open_)
    return;
  ESP_LOGI(TAG, "Opening valve");
  is_open_ = true;
  write_pin_(true);
}

void Valve::close() {
  if (!is_open_)
    return;
  ESP_LOGI(TAG, "Closing valve");
  is_open_ = false;
  write_pin_(false);
}

void Valve::write_pin_(bool valve_open) {
  // For normally-open valves, invert the signal
  pin_->digital_write(normally_open_ ? !valve_open : valve_open);
}

}  // namespace espresso_machine_valve
}  // namespace esphome
