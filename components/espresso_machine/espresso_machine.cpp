#include "espresso_machine.h"
#include "esphome/core/log.h"

namespace esphome {
namespace espresso_machine {

static const char *const TAG = "espresso_machine";

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void EspressoMachine::setup() {
  ESP_LOGI(TAG, "Espresso machine orchestrator initialised");
  ESP_LOGI(TAG, "  brew: target=%.1f°C  flow_max=%.1fml  offset=%.1fml",
           brew_target_temp_, brew_flow_max_ml_, brew_flow_offset_ml_);
  ESP_LOGI(TAG, "  steam: target=%.1f°C  cool_down_to=%.1f°C  flow_max=%.2fml/s",
           steam_target_temp_, steam_cool_down_to_, steam_flow_max_ml_per_s_);
  safe_stop_all_();
}

// ---------------------------------------------------------------------------
// Main loop — advance whichever state machine is active
// ---------------------------------------------------------------------------
void EspressoMachine::loop() {
  switch (mode_) {
    case EspressoMode::BREWING:
      advance_brew_();
      break;
    case EspressoMode::STEAMING:
      advance_steam_();
      break;
    case EspressoMode::IDLE:
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Public actions
// ---------------------------------------------------------------------------
void EspressoMachine::brew_start() {
  if (mode_ != EspressoMode::IDLE) {
    ESP_LOGW(TAG, "brew_start ignored: machine is %s", mode_name());
    return;
  }
  ESP_LOGI(TAG, "Brew START — target=%.1f°C  flow_max=%.1fml", brew_target_temp_, brew_flow_max_ml_);
  mode_ = EspressoMode::BREWING;
  brew_state_ = BrewState::HEATING;
  state_entered_ms_ = millis();

  // Start with all valves closed and pump off until temperature is reached
  if (brew_valve_)
    brew_valve_->close();
  if (brew_purge_valve_)
    brew_purge_valve_->close();
  if (brew_pump_) {
    brew_pump_->turn_off();
    brew_pump_->reset_flow();
  }

  // NOTE: heater setpoint is raised to brew_target_temp_ in Phase 2.
  // The PID climate entity will be referenced here via a climate::ClimateCall.
}

void EspressoMachine::brew_stop() {
  if (mode_ != EspressoMode::BREWING) {
    ESP_LOGW(TAG, "brew_stop ignored: machine is %s", mode_name());
    return;
  }
  ESP_LOGI(TAG, "Brew STOP");
  safe_stop_all_();
  brew_state_ = BrewState::IDLE;
  mode_ = EspressoMode::IDLE;
}

void EspressoMachine::steam_start() {
  if (mode_ != EspressoMode::IDLE) {
    ESP_LOGW(TAG, "steam_start ignored: machine is %s", mode_name());
    return;
  }
  ESP_LOGI(TAG, "Steam START — target=%.1f°C", steam_target_temp_);
  mode_ = EspressoMode::STEAMING;
  steam_state_ = SteamState::HEATING;
  state_entered_ms_ = millis();

  // Close all valves and stop pump while heating to steam temperature
  if (steam_valve_)
    steam_valve_->close();
  if (steam_purge_valve_)
    steam_purge_valve_->close();
  if (steam_pump_)
    steam_pump_->turn_off();

  // NOTE: heater setpoint raised to steam_target_temp_ in Phase 2
}

void EspressoMachine::steam_stop() {
  if (mode_ != EspressoMode::STEAMING) {
    ESP_LOGW(TAG, "steam_stop ignored: machine is %s", mode_name());
    return;
  }
  ESP_LOGI(TAG, "Steam STOP");
  safe_stop_all_();
  steam_state_ = SteamState::IDLE;
  mode_ = EspressoMode::IDLE;

  // NOTE: heater setpoint lowered to steam_cool_down_to_ in Phase 2
}

// ---------------------------------------------------------------------------
// Brew state machine
// ---------------------------------------------------------------------------
void EspressoMachine::advance_brew_() {
  switch (brew_state_) {
    case BrewState::HEATING:
      // Phase 2: transition when temperature sensor reads >= brew_target_temp_.
      // Phase 1 placeholder: transition immediately to BREWING.
      ESP_LOGI(TAG, "Brew: HEATING → BREWING (temperature control wired in Phase 2)");
      brew_state_ = BrewState::BREWING;
      state_entered_ms_ = millis();
      if (brew_valve_)
        brew_valve_->open();
      if (brew_pump_)
        brew_pump_->turn_on();
      break;

    case BrewState::BREWING: {
      // Terminate when flow meter reaches the configured target volume.
      // Flow data is obtained through the pump's IFlowMeter subsystem.
      float volume = brew_pump_ ? brew_pump_->get_flow_total() : 0.0f;
      if (volume >= brew_flow_max_ml_) {
        ESP_LOGI(TAG, "Brew: target volume %.1fml reached (flow_max=%.1fml) → DONE", volume,
                 brew_flow_max_ml_);
        brew_state_ = BrewState::DONE;
        state_entered_ms_ = millis();
        if (brew_pump_)
          brew_pump_->turn_off();
        if (brew_valve_)
          brew_valve_->close();
      }
      break;
    }

    case BrewState::DONE:
      // Phase 9: run cleanup script (purge path) here.
      // Phase 1 placeholder: return to idle immediately.
      ESP_LOGI(TAG, "Brew: DONE → IDLE");
      brew_state_ = BrewState::IDLE;
      mode_ = EspressoMode::IDLE;
      break;

    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Steam state machine
// ---------------------------------------------------------------------------
void EspressoMachine::advance_steam_() {
  switch (steam_state_) {
    case SteamState::HEATING:
      // Phase 2: transition when temperature sensor reads >= steam_target_temp_.
      // Phase 1 placeholder: transition immediately to STEAMING.
      ESP_LOGI(TAG, "Steam: HEATING → STEAMING (temperature control wired in Phase 2)");
      steam_state_ = SteamState::STEAMING;
      state_entered_ms_ = millis();
      if (steam_valve_)
        steam_valve_->open();
      if (steam_pump_)
        steam_pump_->turn_on();
      break;

    case SteamState::STEAMING:
      // Flow-rate control via pump duty cycle comes in Phase 8.
      // For now, pump runs continuously; user calls steam_stop() manually.
      break;

    case SteamState::COOLING:
      // Phase 8: monitor temperature drop to steam_cool_down_to_.
      // Phase 1 placeholder: return to idle immediately.
      ESP_LOGI(TAG, "Steam: COOLING → IDLE");
      steam_state_ = SteamState::IDLE;
      mode_ = EspressoMode::IDLE;
      break;

    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
void EspressoMachine::safe_stop_all_() {
  if (brew_pump_)
    brew_pump_->turn_off();
  if (steam_pump_)
    steam_pump_->turn_off();
  if (brew_valve_)
    brew_valve_->close();
  if (brew_purge_valve_)
    brew_purge_valve_->close();
  if (steam_valve_)
    steam_valve_->close();
  if (steam_purge_valve_)
    steam_purge_valve_->close();
}

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
