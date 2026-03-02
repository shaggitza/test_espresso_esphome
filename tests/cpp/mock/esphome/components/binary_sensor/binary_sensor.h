#pragma once
#include <functional>
#include <vector>

namespace esphome {
namespace binary_sensor {

// Minimal BinarySensor stub for unit tests.
// Supports add_on_state_callback() so display_ui.cpp compiles and tests can
// simulate button presses by calling trigger().
class BinarySensor {
 public:
  bool state{false};

  void add_on_state_callback(std::function<void(bool)> cb) {
    callbacks_.push_back(cb);
  }

  // Test helper: simulate a state change and fire all callbacks.
  void trigger(bool new_state) {
    state = new_state;
    for (auto &cb : callbacks_) cb(new_state);
  }

 private:
  std::vector<std::function<void(bool)>> callbacks_;
};

}  // namespace binary_sensor
}  // namespace esphome
