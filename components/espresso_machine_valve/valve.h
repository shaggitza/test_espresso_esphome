#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "espresso_machine/interfaces.h"

namespace esphome {
namespace espresso_machine_valve {

class Valve : public Component, public espresso_machine::IValve {
 public:
  void set_pin(GPIOPin *pin) { pin_ = pin; }
  void set_normally_open(bool normally_open) { normally_open_ = normally_open; }

  void setup() override;
  void loop() override {}

  void open();
  void close();
  bool is_open() const { return is_open_; }

 protected:
  GPIOPin *pin_{nullptr};
  bool normally_open_{false};
  bool is_open_{false};

  void write_pin_(bool valve_open);
};

}  // namespace espresso_machine_valve
}  // namespace esphome
