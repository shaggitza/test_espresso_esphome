#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/automation.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/number/number.h"
#include "../espresso_machine/interfaces.h"

namespace esphome {
namespace espresso_machine_mock_pump {

class MockPump;

// ---------------------------------------------------------------------------
// MockPumpNumber — number entity for runtime tuning of physics parameters
// ---------------------------------------------------------------------------
class MockPumpNumber : public number::Number, public Component {
 public:
  enum class ParamType { NOMINAL_FLOW, PUCK_TIME_CONSTANT };

  void set_parent(MockPump *parent) { parent_ = parent; }
  void set_param_type(ParamType type) { param_type_ = type; }

  void setup() override;
  void loop() override {}

 protected:
  void control(float value) override;

  MockPump *parent_{nullptr};
  ParamType param_type_{ParamType::NOMINAL_FLOW};
};

// ---------------------------------------------------------------------------
// MockPump — simulated pump with puck wetting flow model
//
// Implements IPump interface so the orchestrator can use it directly.
// Also provides flow data without needing a separate flow meter component.
// ---------------------------------------------------------------------------
class MockPump : public switch_::Switch, public Component, public espresso_machine::IPump {
 public:
  // Configuration setters (called from generated code)
  void set_nominal_flow(float f) { nominal_flow_ = f; }
  void set_puck_time_constant(float t) { puck_time_constant_ = t; }

  // Link to mock heater so flow rate drives thermoblock cooling
  void set_heater(espresso_machine::IFlowObserver *h) { flow_observer_ = h; }

  // Sensor setters
  void set_rate_sensor(sensor::Sensor *s) { rate_sensor_ = s; }
  void set_total_sensor(sensor::Sensor *s) { total_sensor_ = s; }

  // Runtime tuning number entities
  void set_nominal_flow_number(MockPumpNumber *num) {
    nominal_flow_number_ = num;
    if (num) num->set_param_type(MockPumpNumber::ParamType::NOMINAL_FLOW);
  }
  void set_puck_time_constant_number(MockPumpNumber *num) {
    puck_time_constant_number_ = num;
    if (num) num->set_param_type(MockPumpNumber::ParamType::PUCK_TIME_CONSTANT);
  }

  void setup() override;
  void loop() override;

  // IPump interface
  void turn_on() override { write_state(true); }
  void turn_off() override { write_state(false); }
  bool is_running() const override { return running_; }

  // Flow subsystem — simulated based on puck wetting model
  float get_flow_rate() const override { return current_flow_rate_; }
  float get_flow_total() const override { return total_volume_; }
  void reset_flow() override;

  // Runtime parameter accessors/mutators
  float get_nominal_flow() const { return nominal_flow_; }
  float get_puck_time_constant() const { return puck_time_constant_; }

  void update_nominal_flow(float v) { nominal_flow_ = v; }
  void update_puck_time_constant(float v) { puck_time_constant_ = v; }

 protected:
  void write_state(bool state) override;

  // Physics parameters
  float nominal_flow_{4.0f};         // Nominal flow rate [mL/s]
  float puck_time_constant_{10.0f};  // Puck wetting time constant [s]

  // Simulation state
  bool running_{false};
  float run_time_{0.0f};             // Time since pump started [s]
  float current_flow_rate_{0.0f};    // Instantaneous flow rate [mL/s]
  float total_volume_{0.0f};         // Accumulated volume [mL]

  // Sub-entities
  sensor::Sensor *rate_sensor_{nullptr};
  sensor::Sensor *total_sensor_{nullptr};
  MockPumpNumber *nominal_flow_number_{nullptr};
  MockPumpNumber *puck_time_constant_number_{nullptr};
  espresso_machine::IFlowObserver *flow_observer_{nullptr};

  // Timing
  uint32_t last_update_ms_{0};
};

// ---------------------------------------------------------------------------
// Automation actions
// ---------------------------------------------------------------------------

template<typename... Ts>
class ResetAction : public Action<Ts...> {
 public:
  explicit ResetAction(MockPump *parent) : parent_(parent) {}
  void play(Ts... /*x*/) override { parent_->reset_flow(); }

 private:
  MockPump *parent_;
};

}  // namespace espresso_machine_mock_pump
}  // namespace esphome
