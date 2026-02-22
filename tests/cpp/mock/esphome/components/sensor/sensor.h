#pragma once

namespace esphome {
namespace sensor {

// Minimal Sensor stub for unit tests.
// publish_state() stores the value so tests can inspect it.
class Sensor {
 public:
  void publish_state(float value) { state = value; }
  float state{0.0f};
};

}  // namespace sensor
}  // namespace esphome
