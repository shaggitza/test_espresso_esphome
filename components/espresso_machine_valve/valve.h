#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/switch/switch.h"
#include "../espresso_machine/interfaces.h"

#include <vector>

namespace esphome {
namespace espresso_machine_valve {

class Valve : public switch_::Switch, public Component, public espresso_machine::IValve {
 public:
  void set_pin(GPIOPin *pin) { pin_ = pin; }
  void set_normally_open(bool normally_open) { normally_open_ = normally_open; }

  void setup() override;
  void loop() override {}
  ~Valve();

  void open();
  void close();
  bool is_open() const { return is_open_; }

  // Clears the global valve registry — intended for use in unit tests only.
  static void reset_registry();

 protected:
  void write_state(bool state) override;

  GPIOPin *pin_{nullptr};
  bool normally_open_{false};
  bool is_open_{false};

  static std::vector<Valve *> &all_valves_();
  void write_pin_(bool valve_open);
};

// ---------------------------------------------------------------------------
// Automation actions
// ---------------------------------------------------------------------------

template<typename... Ts>
class OpenAction : public Action<Ts...> {
 public:
  explicit OpenAction(Valve *parent) : parent_(parent) {}
  void play(Ts... /*x*/) override { parent_->open(); }

 private:
  Valve *parent_;
};

template<typename... Ts>
class CloseAction : public Action<Ts...> {
 public:
  explicit CloseAction(Valve *parent) : parent_(parent) {}
  void play(Ts... /*x*/) override { parent_->close(); }

 private:
  Valve *parent_;
};

}  // namespace espresso_machine_valve
}  // namespace esphome
