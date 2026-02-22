#pragma once

namespace esphome {
namespace output {

// Minimal FloatOutput stub for unit tests.
// write_state() is called with a value 0.0–1.0 and stores it for inspection.
class FloatOutput {
 public:
  virtual void set_level(float state) { write_state(state); }
  virtual void write_state(float state) { state_ = state; }
  float state_{0.0f};
};

}  // namespace output
}  // namespace esphome
