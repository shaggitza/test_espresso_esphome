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
  enum class ParamType { NOMINAL_FLOW, PUCK_TIME_CONSTANT, PUCK_DENSITY, PUMP_MAX_PRESSURE, INTERNAL_VOLUME, PUCK_ABSORPTION_ML, PUCK_EXTRACTION_TAU };

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
  void set_puck_density(float d) { puck_density_ = d; }
  void set_pump_max_pressure(float p) { pump_max_pressure_bar_ = p; }
  void set_internal_volume(float v) { internal_volume_ml_ = v; }
  void set_puck_absorption(float v) { puck_absorption_ml_ = v; }
  void set_puck_extraction_tau(float t) { puck_extraction_tau_ = t; }

  // Link to mock heater so flow rate drives thermoblock cooling
  void set_heater(espresso_machine::IFlowObserver *h) { flow_observer_ = h; }

  // Sensor setters
  void set_rate_sensor(sensor::Sensor *s) { rate_sensor_ = s; }
  void set_total_sensor(sensor::Sensor *s) { total_sensor_ = s; }
  void set_pressure_sensor(sensor::Sensor *s) { pressure_sensor_ = s; }
  void set_nozzle_rate_sensor(sensor::Sensor *s) { nozzle_rate_sensor_ = s; }
  void set_nozzle_total_sensor(sensor::Sensor *s) { nozzle_total_sensor_ = s; }
  void set_avg_rate_sensor(sensor::Sensor *s) { avg_rate_sensor_ = s; }

  // Runtime tuning number entities
  void set_nominal_flow_number(MockPumpNumber *num) {
    nominal_flow_number_ = num;
    if (num) num->set_param_type(MockPumpNumber::ParamType::NOMINAL_FLOW);
  }
  void set_puck_time_constant_number(MockPumpNumber *num) {
    puck_time_constant_number_ = num;
    if (num) num->set_param_type(MockPumpNumber::ParamType::PUCK_TIME_CONSTANT);
  }
  void set_puck_density_number(MockPumpNumber *num) {
    puck_density_number_ = num;
    if (num) num->set_param_type(MockPumpNumber::ParamType::PUCK_DENSITY);
  }
  void set_pump_max_pressure_number(MockPumpNumber *num) {
    pump_max_pressure_number_ = num;
    if (num) num->set_param_type(MockPumpNumber::ParamType::PUMP_MAX_PRESSURE);
  }
  void set_internal_volume_number(MockPumpNumber *num) {
    internal_volume_number_ = num;
    if (num) num->set_param_type(MockPumpNumber::ParamType::INTERNAL_VOLUME);
  }
  void set_puck_absorption_number(MockPumpNumber *num) {
    puck_absorption_number_ = num;
    if (num) num->set_param_type(MockPumpNumber::ParamType::PUCK_ABSORPTION_ML);
  }
  void set_puck_extraction_tau_number(MockPumpNumber *num) {
    puck_extraction_tau_number_ = num;
    if (num) num->set_param_type(MockPumpNumber::ParamType::PUCK_EXTRACTION_TAU);
  }

  void setup() override;
  void loop() override;

  // IPump interface
  void turn_on() override { write_state(true); }
  void turn_off() override { write_state(false); }
  bool is_running() const override { return running_; }

  // Bypass mode — when pumping through steam/purge valves instead of a puck,
  // there's virtually no resistance. This makes flow = nominal_flow and P = 0.
  void set_bypass_mode(bool bypass) override { open_valve_mode_ = bypass; }
  bool get_bypass_mode() const { return open_valve_mode_; }

  // Flow-rate bang-bang control: stores the target rate; loop() modulates
  // write_state(true/false) to maintain it.  Set to 0 to disable.
  void set_target_flow(float ml_per_s) override { target_flow_rate_ = ml_per_s; }
  float get_target_flow() const { return target_flow_rate_; }

  // Flow subsystem — simulated based on puck wetting model
  float get_flow_rate() const override { return current_flow_rate_; }
  float get_flow_total() const override { return total_volume_; }
  void reset_flow() override;

  // Pressure accessor — instantaneous system pressure [bar]
  float get_system_pressure() const { return system_pressure_bar_; }

  // Runtime parameter accessors/mutators
  float get_nominal_flow() const { return nominal_flow_; }
  float get_puck_time_constant() const { return puck_time_constant_; }
  float get_puck_density() const { return puck_density_; }
  float get_pump_max_pressure() const { return pump_max_pressure_bar_; }
  float get_internal_volume() const { return internal_volume_ml_; }
  float get_puck_absorption() const { return puck_absorption_ml_; }
  float get_puck_extraction_tau() const { return puck_extraction_tau_; }

  void update_nominal_flow(float v) { nominal_flow_ = v; }
  void update_puck_time_constant(float v) { puck_time_constant_ = v; }
  void update_puck_density(float v) { puck_density_ = v; }
  void update_pump_max_pressure(float v) { pump_max_pressure_bar_ = v; }
  void update_internal_volume(float v) { internal_volume_ml_ = v; }
  void update_puck_absorption(float v) { puck_absorption_ml_ = v; }
  void update_puck_extraction_tau(float v) { puck_extraction_tau_ = v; }

  // Nozzle flow accessors (flow exiting the puck into the cup)
  float get_nozzle_flow_rate() const { return nozzle_flow_rate_; }
  float get_nozzle_flow_total() const { return nozzle_total_volume_; }
  // 3-second rolling average of the pump flow rate (12 × 250 ms publish intervals)
  float get_avg_rate_3s() const { return avg_rate_3s_; }

 protected:
  void write_state(bool state) override;

  // Physics parameters
  //
  // Puck density model (replaces bar-based puck pressure):
  //   puck_density = 1..100 dimensionless scale
  //   flow_fraction = (101 − D) / 100     → 1.0 at D=1, 0.01 at D=100
  //   Q_ss          = nominal_flow × flow_fraction
  //   P_equilibrium = pump_max_pressure × (D − 1) / 100
  //                   → 0 bar at D=1 (no resistance), ~P_max at D=100 (stall)
  //
  // Wetting model: time constant scales with density so a denser puck
  // takes proportionally longer to wet before flow breaks through.
  //   effective_τ = puck_time_constant × (D / 100)
  //   wetted_fraction(t) = 1 − exp(−t / effective_τ)
  //   Q(t) = Q_ss × wetted_fraction(t)
  //
  // Puck extraction/degradation model: coffee solubles dissolve and the
  // puck structure weakens as the shot progresses, gradually reducing puck
  // resistance. The effective puck density decreases from its initial value
  // toward 1 (fully open) with time constant puck_extraction_tau_.
  //   effective_D(t) = 1 + (puck_density − 1) × exp(−t / τ_extract)
  //   → at t=0: effective_D = puck_density (initial resistance)
  //   → at t→∞: effective_D → 1 (fully extracted, no resistance)
  //   Both Q_ss and P_equilibrium are computed from effective_D, so flow
  //   increases monotonically throughout the shot. Set puck_extraction_tau_
  //   to 0 to disable this model and keep constant puck resistance.
  //
  // Pressure model: fast first-order rise toward P_equilibrium (τ = 1.5 s).
  //   This makes pressure build quickly as observed in a real machine,
  //   then stabilise at the puck back-pressure.  At D=100 pressure climbs
  //   to near the pump stall pressure since no water can escape.
  float nominal_flow_{4.0f};           // Max unimpeded flow (D=1) [mL/s]
  float puck_time_constant_{10.0f};    // Wetting time constant at D=100 [s]
  float puck_density_{50.0f};          // Puck density: 1=open, 100=blocked
  float pump_max_pressure_bar_{15.0f}; // Pump stall pressure [bar] (Ulka EP5 ≈ 15 bar)
  // Internal volume of tubing and piping inside the machine [mL].
  // When the pump stops, this trapped pressurized volume continues to drive
  // flow through the puck until the pressure bleeds off.
  // τ_decay = internal_volume_ml / nominal_flow  (e.g. 20mL / 4mL·s⁻¹ = 5 s)
  // Set to 0 to disable this model and revert to the legacy instant-decay behaviour.
  float internal_volume_ml_{20.0f};
  // Puck absorption capacity [mL].
  // Coffee grounds absorb water during extraction (~2ml per gram of coffee).
  // A typical 18g dose absorbs ~36ml. This absorption happens primarily during
  // the wetting phase and reduces nozzle output compared to pump input.
  // The absorption follows an exponential saturation model tied to puck wetting.
  float puck_absorption_ml_{36.0f};
  // Puck extraction time constant [s].
  // As coffee solubles dissolve and the puck structure weakens, puck resistance
  // decreases over the shot. Typical shots (25–35 s) show significant flow
  // increase. A default of 45 s gives a realistic profile where effective density
  // drops to ~50% of initial by the end of a 30 s shot.
  // Set to 0 to disable degradation (constant puck density throughout the shot).
  float puck_extraction_tau_{45.0f};

  // Simulation state
  bool running_{false};
  bool open_valve_mode_{false};       // If true, bypass puck model (valve open)
  float target_flow_rate_{0.0f};      // 0 = disabled; >0 = bang-bang target [mL/s]
  float run_time_{0.0f};              // Time since pump started [s]
  float current_flow_rate_{0.0f};     // Instantaneous flow rate [mL/s]
  float total_volume_{0.0f};          // Accumulated pump volume [mL]
  float nozzle_total_volume_{0.0f};   // Accumulated nozzle output volume [mL]
  float nozzle_flow_rate_{0.0f};      // Instantaneous nozzle flow rate [mL/s]
  float absorbed_volume_{0.0f};       // Water absorbed by puck so far [mL]
  float system_pressure_bar_{0.0f};   // System pressure [bar]; tracks puck back-pressure

  // Sub-entities
  sensor::Sensor *rate_sensor_{nullptr};
  sensor::Sensor *total_sensor_{nullptr};
  sensor::Sensor *pressure_sensor_{nullptr};
  sensor::Sensor *nozzle_rate_sensor_{nullptr};
  sensor::Sensor *nozzle_total_sensor_{nullptr};
  sensor::Sensor *avg_rate_sensor_{nullptr};

  // 3-second rolling average (12 samples × 250 ms publish interval)
  static constexpr uint8_t AVG_WINDOW_SIZE = 12;
  float avg_buf_[AVG_WINDOW_SIZE]{};
  uint8_t avg_buf_idx_{0};
  uint8_t avg_buf_count_{0};
  float avg_rate_3s_{0.0f};
  MockPumpNumber *nominal_flow_number_{nullptr};
  MockPumpNumber *puck_time_constant_number_{nullptr};
  MockPumpNumber *puck_density_number_{nullptr};
  MockPumpNumber *pump_max_pressure_number_{nullptr};
  MockPumpNumber *internal_volume_number_{nullptr};
  MockPumpNumber *puck_absorption_number_{nullptr};
  MockPumpNumber *puck_extraction_tau_number_{nullptr};
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
