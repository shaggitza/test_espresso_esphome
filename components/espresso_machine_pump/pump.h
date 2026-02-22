#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/number/number.h"
#include "../espresso_machine/interfaces.h"

namespace esphome {
namespace espresso_machine_pump {

// ---------------------------------------------------------------------------
// PumpSwitch — relay pump exposed as an ESPHome switch entity
// ---------------------------------------------------------------------------
class PumpSwitch : public switch_::Switch, public Component, public espresso_machine::IPump {
 public:
  void set_pin(GPIOPin *pin) { pin_ = pin; }

  void setup() override;
  void loop() override;

  // IPump interface
  void turn_on() override { write_state(true); }
  void turn_off() override { write_state(false); }
  bool is_running() const override { return running_; }

  // Timed run — turns on the pump and stops after duration_ms.
  void run(uint32_t duration_ms);

 protected:
  void write_state(bool state) override;

  GPIOPin *pin_{nullptr};
  bool running_{false};
  bool run_timed_{false};
  uint32_t run_until_ms_{0};
};

// ---------------------------------------------------------------------------
// PumpNumber — dimmer pump exposed as an ESPHome number entity (0–100 %)
// ---------------------------------------------------------------------------
class PumpNumber : public number::Number, public Component, public espresso_machine::IPump {
 public:
  void set_pin(GPIOPin *pin) { pin_ = pin; }

  void setup() override;
  void loop() override {}

  // IPump interface
  void turn_on() override;
  void turn_off() override;
  bool is_running() const override { return running_; }

 protected:
  void control(float value) override;

  GPIOPin *pin_{nullptr};
  bool running_{false};
  float speed_{0.0f};
};

// ---------------------------------------------------------------------------
// Automation actions
// ---------------------------------------------------------------------------

template<typename... Ts>
class RunAction : public Action<Ts...> {
 public:
  explicit RunAction(PumpSwitch *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(uint32_t, duration_ms)
  void play(Ts... x) override { parent_->run(this->duration_ms_.value(x...)); }

 private:
  PumpSwitch *parent_;
};

}  // namespace espresso_machine_pump
}  // namespace esphome
