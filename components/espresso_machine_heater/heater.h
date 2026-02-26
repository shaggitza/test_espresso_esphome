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

  // Configures how close the current temperature must be to the target before
  // the heater is considered "ready" (i.e. the machine may proceed to brewing
  // or steaming).  A tolerance of 0.5°C means 89.5°C is ready when the target
  // is 90.0°C — preventing an indefinite wait when the thermoblock stabilises
  // just below the setpoint.  Defaults to 0.5°C.
  void set_temperature_tolerance(float tolerance) { temperature_tolerance_ = tolerance; }

  // IHeater — temperature reading
  float get_current_temperature() const override {
    return climate_ ? climate_->current_temperature : 0.0f;
  }

  // IHeater — readiness check.  Returns true when the current temperature is
  // within `temperature_tolerance_` degrees below the target (or above it).
  // This prevents the machine from waiting indefinitely for a thermoblock that
  // has stabilised slightly below the setpoint due to PID steady-state error.
  bool is_ready(float target_temp) const override {
    return get_current_temperature() >= (target_temp - temperature_tolerance_);
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
  float temperature_tolerance_{0.5f};  // °C below target still considered "ready"
};

}  // namespace espresso_machine_heater
}  // namespace esphome
