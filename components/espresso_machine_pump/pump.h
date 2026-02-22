#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "espresso_machine/interfaces.h"

namespace esphome {
namespace espresso_machine_pump {

enum class PumpType : uint8_t {
  RELAY = 0,
  DIMMER = 1,
};

class Pump : public Component, public espresso_machine::IPump {
 public:
  void set_pin(GPIOPin *pin) { pin_ = pin; }
  void set_pump_type(PumpType type) { type_ = type; }

  void setup() override;
  void loop() override {}

  void turn_on();
  void turn_off();
  bool is_running() const { return running_; }

 protected:
  GPIOPin *pin_{nullptr};
  PumpType type_{PumpType::RELAY};
  bool running_{false};
};

}  // namespace espresso_machine_pump
}  // namespace esphome
