#pragma once

#include <string>

namespace esphome {
namespace text_sensor {

// Minimal TextSensor stub for unit tests.
// publish_state() is virtual so tests can subclass and intercept publishes.
class TextSensor {
 public:
  virtual void publish_state(const std::string &value) { state = value; }
  std::string state;
  virtual ~TextSensor() = default;
};

}  // namespace text_sensor
}  // namespace esphome
