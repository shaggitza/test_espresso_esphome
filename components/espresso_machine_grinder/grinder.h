#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/button/button.h"
#include "esphome/components/number/number.h"

namespace esphome {
namespace espresso_machine_grinder {

enum class GrinderType : uint8_t {
  RELAY = 0,
  NONE = 1,
};

// ---------------------------------------------------------------------------
// GrinderTimeNumber — number entity exposing the default grind time to HA
// ---------------------------------------------------------------------------
class Grinder;  // forward declaration

class GrinderTimeNumber : public number::Number {
 public:
  GrinderTimeNumber() = default;
  explicit GrinderTimeNumber(Grinder *parent) : parent_(parent) {}
  void set_parent(Grinder *parent) { parent_ = parent; }

 protected:
  void control(float value) override;

  Grinder *parent_{nullptr};
};

// ---------------------------------------------------------------------------
// Grinder — button entity that triggers a timed relay grind.
// The grinder is fully independent of the espresso machine orchestrator.
// ---------------------------------------------------------------------------
class Grinder : public button::Button, public Component {
 public:
  void set_pin(GPIOPin *pin) { pin_ = pin; }
  void set_grinder_type(GrinderType type) { type_ = type; }
  void set_default_grind_time(uint32_t ms) { default_grind_time_ms_ = ms; }
  void set_grind_time_number(GrinderTimeNumber *n) { grind_time_number_ = n; }

  void setup() override;
  void loop() override;

  void grind(uint32_t duration_ms = 0);
  void stop();
  bool is_grinding() const { return grinding_; }

 protected:
  void press_action() override { grind(); }

  GPIOPin *pin_{nullptr};
  GrinderType type_{GrinderType::RELAY};
  uint32_t default_grind_time_ms_{7000};
  bool grinding_{false};
  uint32_t grind_end_ms_{0};
  GrinderTimeNumber *grind_time_number_{nullptr};
};

// ---------------------------------------------------------------------------
// Automation action
// ---------------------------------------------------------------------------

template<typename... Ts>
class GrindAction : public Action<Ts...> {
 public:
  explicit GrindAction(Grinder *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(uint32_t, duration_ms)
  void play(Ts... x) override { parent_->grind(this->duration_ms_.value(x...)); }

 private:
  Grinder *parent_;
};

}  // namespace espresso_machine_grinder
}  // namespace esphome
