#include "pump.h"
#include "esphome/core/log.h"

namespace esphome {
namespace espresso_machine_pump {

static const char *const TAG = "espresso_machine_pump";

// ---------------------------------------------------------------------------
// PumpSwitch (relay)
// ---------------------------------------------------------------------------

void PumpSwitch::setup() {
  ESP_LOGI(TAG, "PumpSwitch (relay) initialised");
  pin_->setup();
  pin_->digital_write(false);
  publish_state(false);
}

void PumpSwitch::loop() {
  if (run_timed_ && running_ && (int32_t)(millis() - run_until_ms_) >= 0) {
    run_timed_ = false;
    write_state(false);
  }
}

void PumpSwitch::write_state(bool state) {
  if (state) {
    if (running_)
      return;
    ESP_LOGI(TAG, "Pump ON");
    running_ = true;
    pin_->digital_write(true);
    publish_state(true);
  } else {
    if (!running_)
      return;
    ESP_LOGI(TAG, "Pump OFF");
    running_ = false;
    run_timed_ = false;
    pin_->digital_write(false);
    publish_state(false);
  }
}

void PumpSwitch::run(uint32_t duration_ms) {
  if (duration_ms > 0) {
    run_timed_ = true;
    run_until_ms_ = millis() + duration_ms;
  }
  write_state(true);
}

// ---------------------------------------------------------------------------
// PumpNumber (dimmer)
// ---------------------------------------------------------------------------

void PumpNumber::setup() {
  ESP_LOGI(TAG, "PumpNumber (dimmer) initialised");
  pin_->setup();
  pin_->digital_write(false);
  publish_state(0.0f);
}

void PumpNumber::control(float value) {
  speed_ = value;
  running_ = (value > 0.0f);
  // In real hardware the pin would drive a slow_pwm output; here we just
  // write a digital on/off for relay-mode fallback.
  pin_->digital_write(running_);
  publish_state(value);
  ESP_LOGI(TAG, "Pump speed set to %.0f%%", value);
}

void PumpNumber::turn_on() {
  control(100.0f);
}

void PumpNumber::turn_off() {
  control(0.0f);
}

}  // namespace espresso_machine_pump
}  // namespace esphome
