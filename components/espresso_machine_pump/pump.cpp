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
  if (!running_)
    return;

  // Volume exit: auto-stop when the target volume is reached.
  if (run_volume_active_ && flow_meter_ != nullptr) {
    float dispensed = flow_meter_->get_total_volume();
    if (dispensed >= run_target_volume_ml_) {
      ESP_LOGI(TAG, "Pump run complete: %.1fml dispensed (target %.1fml)", dispensed,
               run_target_volume_ml_);
      run_volume_active_ = false;
      run_timeout_active_ = false;
      write_state(false);
      return;
    }
  }

  // Safety timeout exit: last-resort stop if no flow meter or meter stalls.
  if (run_timeout_active_ && (int32_t)(millis() - run_timeout_end_ms_) >= 0) {
    ESP_LOGW(TAG, "Pump run safety timeout reached — stopping");
    run_volume_active_ = false;
    run_timeout_active_ = false;
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
    run_volume_active_ = false;
    run_timeout_active_ = false;
    pin_->digital_write(false);
    publish_state(false);
  }
}

void PumpSwitch::run(float volume_ml, uint32_t timeout_ms) {
  // Reset flow counter so we measure only what this run dispenses.
  reset_flow();

  run_target_volume_ml_ = volume_ml;
  run_volume_active_ = (volume_ml > 0.0f);

  if (timeout_ms > 0) {
    run_timeout_active_ = true;
    run_timeout_end_ms_ = millis() + timeout_ms;
  } else {
    run_timeout_active_ = false;
  }

  ESP_LOGI(TAG, "Pump run requested: target=%.1fml timeout=%ums", volume_ml, timeout_ms);
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
