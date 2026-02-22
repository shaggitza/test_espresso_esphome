#pragma once

namespace esphome {
namespace output {

// Minimal FloatOutput stub for unit tests.
// set_level() forwards directly to write_state() so tests can drive the mock
// heater without needing ESPHome's full output infrastructure.
class FloatOutput {
 public:
  void set_level(float state) { write_state(state); }

 protected:
  virtual void write_state(float state) = 0;
};

}  // namespace output
}  // namespace esphome
