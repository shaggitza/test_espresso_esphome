#pragma once

namespace esphome {
namespace switch_ {

// Minimal Switch stub for unit tests.
// publish_state() stores the value so tests can inspect it.
// turn_on()/turn_off() are the public API that delegates to write_state().
class Switch {
 public:
  bool state{false};
  void publish_state(bool value) { state = value; }
  void turn_on() { write_state(true); }
  void turn_off() { write_state(false); }
  void toggle() { write_state(!state); }

 protected:
  virtual void write_state(bool state) = 0;
};

}  // namespace switch_
}  // namespace esphome
