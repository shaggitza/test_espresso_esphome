#pragma once

#include <string>

namespace esphome {
namespace text_sensor {

// Minimal TextSensor stub for unit tests.
// publish_state() stores the value so tests can inspect it.
class TextSensor {
 public:
  void publish_state(const std::string &value) { state = value; }
  std::string state;
};

}  // namespace text_sensor
}  // namespace esphome
