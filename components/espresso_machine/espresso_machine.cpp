#include "espresso_machine.h"
#include "esphome/core/log.h"

namespace esphome {
namespace espresso_machine {

static const char *const TAG = "espresso_machine";

void EspressoMachine::setup() {
  ESP_LOGI(TAG, "Espresso machine orchestrator initialised");
}

void EspressoMachine::loop() {}

const char *EspressoMachine::mode_name() const {
  switch (mode_) {
    case EspressoMode::IDLE:
      return "idle";
    case EspressoMode::BREWING:
      return "brewing";
    case EspressoMode::STEAMING:
      return "steaming";
    default:
      return "unknown";
  }
}

}  // namespace espresso_machine
}  // namespace esphome
