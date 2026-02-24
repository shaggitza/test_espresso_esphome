#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/climate/climate.h"
#include "../espresso_machine/interfaces.h"

namespace esphome {
namespace espresso_machine_heater {

// ---------------------------------------------------------------------------
// EspressoMachineHeater — production IHeater adapter for ESPHome climate.pid
//
// Wraps any ESPHome climate entity (typically climate.pid) and exposes it
// through the IHeater interface used by the EspressoMachine orchestrator.
//
// Usage in YAML:
//   espresso_machine_heater:
//     id: brew_heater_ctrl
//     climate_id: main_heater
//
//   espresso_machine:
//     brew:
//       heater_controller: brew_heater_ctrl
//     steam:
//       heater_controller: brew_heater_ctrl
// ---------------------------------------------------------------------------
class EspressoMachineHeater : public Component,
                              public espresso_machine::IHeater {
 public:
  void set_climate(climate::Climate *climate) { climate_ = climate; }

  // IHeater — temperature reading
  float get_current_temperature() const override {
    return climate_ ? climate_->current_temperature : 0.0f;
  }

  // IHeater — setpoint command
  void set_target_temperature(float t) override {
    if (climate_ == nullptr)
      return;
    auto call = climate_->make_call();
    call.set_target_temperature(t);
    call.perform();
  }

  // IHeater — force heater off (safety cutoff)
  void force_off() override {
    if (climate_ == nullptr)
      return;
    auto call = climate_->make_call();
    call.set_mode(climate::CLIMATE_MODE_OFF);
    call.perform();
  }

  void setup() override {
    ESP_LOGI("espresso_machine_heater", "EspressoMachineHeater adapter initialized");
  }

  void loop() override {}

 protected:
  climate::Climate *climate_{nullptr};
};

}  // namespace espresso_machine_heater
}  // namespace esphome
