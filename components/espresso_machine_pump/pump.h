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
  void set_flow_meter(espresso_machine::IFlowMeter *fm) { flow_meter_ = fm; }

  void setup() override;
  void loop() override;

  // IPump interface
  void turn_on() override { write_state(true); }
  void turn_off() override { write_state(false); }
  bool is_running() const override { return running_; }

  // Flow subsystem — delegates to the wired flow meter if present
  float get_flow_rate() const override {
    return flow_meter_ ? flow_meter_->get_rate() : 0.0f;
  }
  float get_flow_total() const override {
    return flow_meter_ ? flow_meter_->get_total_volume() : 0.0f;
  }
  void reset_flow() override {
    if (flow_meter_)
      flow_meter_->reset();
  }

  // Volume-driven run — resets the flow counter, starts the pump, and
  // auto-stops when the flow meter reports >= volume_ml dispensed.
  // If no flow meter is wired the pump runs until an external signal
  // (stop button, temperature safety, etc.) calls turn_off().
  // timeout_ms is an optional safety cap; 0 means no timeout.
  void run(float volume_ml, uint32_t timeout_ms = 0);

 protected:
  void write_state(bool state) override;

  GPIOPin *pin_{nullptr};
  espresso_machine::IFlowMeter *flow_meter_{nullptr};
  bool running_{false};

  // Volume-run tracking
  bool run_volume_active_{false};
  float run_target_volume_ml_{0.0f};

  // Safety timeout tracking
  bool run_timeout_active_{false};
  uint32_t run_timeout_end_ms_{0};
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
  TEMPLATABLE_VALUE(float, volume_ml)
  TEMPLATABLE_VALUE(uint32_t, timeout_ms)
  void play(Ts... x) override {
    parent_->run(this->volume_ml_.value(x...), this->timeout_ms_.value(x...));
  }

 private:
  PumpSwitch *parent_;
};

}  // namespace espresso_machine_pump
}  // namespace esphome
