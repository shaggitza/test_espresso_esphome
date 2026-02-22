#pragma once

namespace esphome {
namespace number {

// Minimal Number stub for unit tests.
// publish_state() stores the value so tests can inspect it.
// control() is the abstract callback invoked when HA sends a new value.
class Number {
 public:
  float state{0.0f};
  void publish_state(float value) { state = value; }

 protected:
  virtual void control(float value) = 0;
};

}  // namespace number
}  // namespace esphome
