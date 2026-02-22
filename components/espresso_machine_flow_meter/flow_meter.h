#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/automation.h"
#include "esphome/components/sensor/sensor.h"
#include "../espresso_machine/interfaces.h"

namespace esphome {
namespace espresso_machine_flow_meter {

class FlowMeter : public Component, public espresso_machine::IFlowMeter {
 public:
  void set_pin(InternalGPIOPin *pin) { pin_ = pin; }
  void set_pulses_per_ml(float pulses_per_ml) { pulses_per_ml_ = pulses_per_ml; }

  // Child sensor setters — called from generated code when the user declares
  // rate_sensor: / total_sensor: sub-blocks in YAML.
  void set_rate_sensor(sensor::Sensor *s) { rate_sensor_ = s; }
  void set_total_sensor(sensor::Sensor *s) { total_sensor_ = s; }

  void setup() override;
  void loop() override;

  float get_rate() const { return rate_; }
  float get_total_volume() const { return total_volume_; }
  // For use in display lambdas
  float total_volume() const { return total_volume_; }
  void reset();

  // Adjust pulses_per_ml so that the pulses counted since the last reset
  // correspond to actual_volume_ml of dispensed liquid.
  void calibrate(float actual_volume_ml);

  // ISR — called on every rising edge of the flow meter pulse pin.
  static void IRAM_ATTR pulse_isr(FlowMeter *self) { self->pulse_count_++; }

  // Simulates ISR-delivered pulses (used in unit tests and for calibration)
  void add_pulses(uint32_t count) { pulse_count_ += count; }

 protected:
  InternalGPIOPin *pin_{nullptr};
  float pulses_per_ml_{1.0f};
  volatile uint32_t pulse_count_{0};
  uint32_t last_pulse_count_{0};
  uint32_t last_update_ms_{0};
  float rate_{0.0f};
  float total_volume_{0.0f};

  sensor::Sensor *rate_sensor_{nullptr};
  sensor::Sensor *total_sensor_{nullptr};
};

// ---------------------------------------------------------------------------
// Automation actions
// ---------------------------------------------------------------------------

template<typename... Ts>
class ResetAction : public Action<Ts...> {
 public:
  explicit ResetAction(FlowMeter *parent) : parent_(parent) {}
  void play(Ts... /*x*/) override { parent_->reset(); }

 private:
  FlowMeter *parent_;
};

template<typename... Ts>
class CalibrateAction : public Action<Ts...> {
 public:
  explicit CalibrateAction(FlowMeter *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(float, actual_volume_ml)
  void play(Ts... x) override { parent_->calibrate(this->actual_volume_ml_.value(x...)); }

 private:
  FlowMeter *parent_;
};

}  // namespace espresso_machine_flow_meter
}  // namespace esphome
