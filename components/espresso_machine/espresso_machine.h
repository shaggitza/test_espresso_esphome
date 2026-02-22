#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace espresso_machine {

enum class EspressoMode : uint8_t {
  IDLE = 0,
  BREWING = 1,
  STEAMING = 2,
};

class EspressoMachine : public Component {
 public:
  void setup() override;
  void loop() override;

  EspressoMode get_mode() const { return mode_; }
  const char *mode_name() const;

 protected:
  EspressoMode mode_{EspressoMode::IDLE};
};

}  // namespace espresso_machine
}  // namespace esphome
