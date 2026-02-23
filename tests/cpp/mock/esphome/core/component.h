#pragma once
#include <cstdint>

namespace esphome {

class Component {
 public:
  virtual void setup() {}
  virtual void loop() {}
  virtual ~Component() = default;
};

// Minimal PollingComponent stub — in tests, call update() directly
class PollingComponent : public Component {
 public:
  virtual void update() = 0;
};

}  // namespace esphome
