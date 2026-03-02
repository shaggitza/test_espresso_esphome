#pragma once
#include <functional>
#include <vector>

namespace esphome {
namespace sensor {

// Minimal Sensor stub for unit tests.
// publish_state() stores the value so tests can inspect it.
// add_on_state_callback() is needed for components like EspressoMachineDisplay
// that subscribe to encoder rotation events.
class Sensor {
 public:
  void publish_state(float value) { state = value; }

  void add_on_state_callback(std::function<void(float)> cb) {
    callbacks_.push_back(cb);
  }

  // Test helper: simulate a sensor reading and fire all callbacks.
  void trigger(float value) {
    state = value;
    for (auto &cb : callbacks_) cb(value);
  }

  float state{0.0f};

 private:
  std::vector<std::function<void(float)>> callbacks_;
};

}  // namespace sensor
}  // namespace esphome
