#include "valve.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <vector>

namespace esphome {
namespace espresso_machine_valve {

static const char *const TAG = "espresso_machine_valve";

std::vector<Valve *> &Valve::all_valves_() {
  static std::vector<Valve *> instances;
  return instances;
}

void Valve::reset_registry() { all_valves_().clear(); }

Valve::~Valve() {
  auto &vec = all_valves_();
  vec.erase(std::remove(vec.begin(), vec.end(), this), vec.end());
}

void Valve::setup() {
  all_valves_().push_back(this);
  ESP_LOGI(TAG, "Valve initialised (normally_open=%s)", normally_open_ ? "true" : "false");
  pin_->setup();
  // Safe default: ensure valve is closed on boot
  write_pin_(false);
  publish_state(false);
}

void Valve::open() {
  // Interlock: close all other registered valves before opening this one.
  for (Valve *v : all_valves_()) {
    if (v != this && v->is_open_) {
      v->close();
    }
  }
  if (is_open_)
    return;
  ESP_LOGI(TAG, "Opening valve");
  is_open_ = true;
  write_pin_(true);
  publish_state(true);
}

void Valve::close() {
  if (!is_open_)
    return;
  ESP_LOGI(TAG, "Closing valve");
  is_open_ = false;
  write_pin_(false);
  publish_state(false);
}

void Valve::write_state(bool state) {
  if (state) {
    open();
  } else {
    close();
  }
}

void Valve::write_pin_(bool valve_open) {
  // For normally-open valves, invert the signal
  pin_->digital_write(normally_open_ ? !valve_open : valve_open);
}

}  // namespace espresso_machine_valve
}  // namespace esphome
