#pragma once

#include <cmath>
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/number/number.h"
#include "../espresso_machine/interfaces.h"

namespace esphome {
namespace espresso_machine_mock_pump {

// Forward declaration so MockPumpParamNumber can reference MockPump.
class MockPump;

// ---------------------------------------------------------------------------
// MockPumpParam — identifies which pump parameter a number entity controls
// ---------------------------------------------------------------------------
enum class MockPumpParam : uint8_t {
  NOMINAL_FLOW = 0,
  PUCK_TAU = 1,
};

// ---------------------------------------------------------------------------
// MockPumpParamNumber — number entity that updates a single pump parameter
// ---------------------------------------------------------------------------
class MockPumpParamNumber : public number::Number {
 public:
  MockPumpParamNumber() = default;
  void set_parent(MockPump *parent, MockPumpParam param) {
    parent_ = parent;
    param_ = param;
  }

 protected:
  void control(float value) override;

  MockPump *parent_{nullptr};
  MockPumpParam param_{MockPumpParam::NOMINAL_FLOW};
};

// ---------------------------------------------------------------------------
// MockPump — physics-based pump + puck resistance simulator
//
// Implements espresso_machine::IPump so the orchestrator interacts with it
// identically to a real PumpSwitch + FlowMeter.  No GPIO or relay is involved.
//
// Puck resistance / wetting model (applied in loop() every 50 ms):
//   Q(t_on) = nominal_flow × (1 − exp(−t_on / puck_time_constant))
//
// Where t_on is cumulative pump-on time since the last reset_flow() call.
// Volume is integrated numerically: V += Q × dt for each loop interval.
//
// This models the physical reality of water forcing through a dry coffee puck:
//   t=0:     Q ≈ 0 ml/s  (puck blocks flow, maximum resistance)
//   t=tau:   Q ≈ 0.63 × nominal  (puck 63% saturated)
//   t=3×tau: Q ≈ 0.95 × nominal  (puck nearly fully saturated)
// ---------------------------------------------------------------------------
class MockPump : public Component, public espresso_machine::IPump {
 public:
  // ----- Static configuration setters (called from Python codegen) ----------
  void set_nominal_flow_ml_per_s(float f) { nominal_flow_ = f; }
  void set_puck_time_constant_s(float tau) {
    if (tau > 0.0f)
      puck_tau_ = tau;
  }

  // ----- Child entity setters -----------------------------------------------
  void set_nominal_flow_number(MockPumpParamNumber *n) { nominal_flow_number_ = n; }
  void set_puck_tau_number(MockPumpParamNumber *n) { puck_tau_number_ = n; }

  // ----- Runtime parameter updates (called by MockPumpParamNumber) ----------
  void set_nominal_flow_runtime(float f) {
    if (f > 0.0f)
      nominal_flow_ = f;
  }
  void set_puck_tau_runtime(float tau) {
    if (tau > 0.0f)
      puck_tau_ = tau;
  }

  // ----- ESPHome lifecycle --------------------------------------------------
  void setup() override;
  void loop() override;

  // ----- IPump interface ----------------------------------------------------
  void turn_on() override;
  void turn_off() override;
  bool is_running() const override { return running_; }
  float get_flow_rate() const override { return flow_rate_; }
  float get_flow_total() const override { return flow_total_; }
  void reset_flow() override;

 protected:
  // -- Pump parameters (may be updated at runtime via number entities) -------
  float nominal_flow_{4.0f};   // ml/s at full puck saturation
  float puck_tau_{10.0f};      // puck wetting time constant (s)

  // -- Simulation state ------------------------------------------------------
  bool running_{false};
  float time_on_s_{0.0f};    // cumulative pump-on time since last reset (s)
  float flow_rate_{0.0f};    // current simulated flow rate (ml/s)
  float flow_total_{0.0f};   // accumulated volume since last reset (ml)
  uint32_t last_update_ms_{0};

  // -- Child entities --------------------------------------------------------
  MockPumpParamNumber *nominal_flow_number_{nullptr};
  MockPumpParamNumber *puck_tau_number_{nullptr};
};

}  // namespace espresso_machine_mock_pump
}  // namespace esphome
