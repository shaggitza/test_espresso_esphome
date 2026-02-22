#pragma once

namespace esphome {
namespace button {

// Minimal Button stub for unit tests.
// press() triggers press_action(), which subclasses implement.
class Button {
 public:
  void press() { press_action(); }

 protected:
  virtual void press_action() = 0;
};

}  // namespace button
}  // namespace esphome
