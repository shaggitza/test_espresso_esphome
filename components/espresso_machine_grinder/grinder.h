#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace espresso_machine_grinder {

enum class GrinderType : uint8_t {
  RELAY = 0,
  NONE = 1,
};

class Grinder : public Component {
 public:
  void set_pin(GPIOPin *pin) { pin_ = pin; }
  void set_grinder_type(GrinderType type) { type_ = type; }
  void set_default_grind_time(uint32_t ms) { default_grind_time_ms_ = ms; }

  void setup() override;
  void loop() override;

  void grind(uint32_t duration_ms = 0);
  void stop();
  bool is_grinding() const { return grinding_; }

 protected:
  GPIOPin *pin_{nullptr};
  GrinderType type_{GrinderType::RELAY};
  uint32_t default_grind_time_ms_{7000};
  bool grinding_{false};
  uint32_t grind_end_ms_{0};
};

}  // namespace espresso_machine_grinder
}  // namespace esphome
