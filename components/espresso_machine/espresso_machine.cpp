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

  // Raise heater setpoint to steam temperature if a controller is wired
  if (steam_heater_ctrl_)
    steam_heater_ctrl_->set_target_temperature(steam_target_temp_);
}

void EspressoMachine::steam_stop() {
  if (mode_ != EspressoMode::STEAMING) {
    ESP_LOGW(TAG, "steam_stop ignored: machine is %s", mode_name());
    return;
  }
  if (steam_state_ == SteamState::HEATING) {
    // Cancel before steaming began — lower setpoint and stop immediately.
    ESP_LOGI(TAG, "Steam STOP (cancelled during heat-up)");
    if (steam_heater_ctrl_)
      steam_heater_ctrl_->set_target_temperature(steam_cool_down_to_);
    safe_stop_all_();
    steam_state_ = SteamState::IDLE;
    mode_ = EspressoMode::IDLE;
    return;
  }
  if (steam_state_ != SteamState::STEAMING) {
    ESP_LOGW(TAG, "steam_stop ignored: not in steaming state");
    return;
  }
  ESP_LOGI(TAG, "Steam STOP — entering cool-down");
  if (steam_valve_)
    steam_valve_->close();
  // Open purge valve immediately when steaming stops — this flushes the steam
  // path and keeps it purging throughout the COOLING state while the thermoblock
  // cools to steam_cool_down_to_.  The valve is closed in the CLEANUP state.
  if (steam_purge_valve_)
    steam_purge_valve_->open();
  if (steam_pump_)
    steam_pump_->turn_off();
  steam_state_ = SteamState::COOLING;
  state_entered_ms_ = millis();
  // Lower heater setpoint to cool-down temperature
  if (steam_heater_ctrl_)
    steam_heater_ctrl_->set_target_temperature(steam_cool_down_to_);
}

// ---------------------------------------------------------------------------
// Brew state machine
// ---------------------------------------------------------------------------
void EspressoMachine::advance_brew_() {
  switch (brew_state_) {
    case BrewState::HEATING:
      // Phase 2: transition when temperature sensor reads >= brew_target_temp_.
      // Phase 7 placeholder: transition immediately (no climate wiring yet).
      state_entered_ms_ = millis();
      if (pre_infusion_enabled_) {
        ESP_LOGI(TAG, "Brew: HEATING → PRE_INFUSION");
        brew_state_ = BrewState::PRE_INFUSION;
        pre_infusion_flowing_ = true;
        if (brew_pump_)
          brew_pump_->reset_flow();
        if (brew_valve_)
          brew_valve_->open();
        if (brew_pump_)
          brew_pump_->turn_on();
      } else {
        ESP_LOGI(TAG, "Brew: HEATING → BREWING");
        enter_brewing_();
      }
      break;

    case BrewState::PRE_INFUSION: {
      if (pre_infusion_flowing_) {
        float volume = brew_pump_ ? brew_pump_->get_flow_total() : 0.0f;
        if (volume >= pre_infusion_volume_ml_) {
          ESP_LOGI(TAG, "Brew: pre-infusion %.1fml → holding for %ums",
                   volume, pre_infusion_hold_time_ms_);
          if (brew_pump_)
            brew_pump_->turn_off();
          pre_infusion_flowing_ = false;
          pre_infusion_hold_start_ms_ = millis();
        }
      } else {
        if ((millis() - pre_infusion_hold_start_ms_) >= pre_infusion_hold_time_ms_) {
          ESP_LOGI(TAG, "Brew: PRE_INFUSION hold complete → BREWING");
          enter_brewing_();
        }
      }
      break;
    }

    case BrewState::BREWING: {
      // Temperature surfing: linearly ramp the desired setpoint from
      // (target + offset) back to target over brew_temp_ramp_time_ms_.
      // NOTE: actual climate setpoint call is wired in Phase 2.
      if (brew_temp_offset_ > 0.0f && brew_temp_ramp_time_ms_ > 0) {
        uint32_t elapsed = millis() - brew_shot_start_ms_;
        float desired_temp;
        if (elapsed >= brew_temp_ramp_time_ms_) {
          desired_temp = brew_target_temp_;
        } else {
          float frac = 1.0f - (static_cast<float>(elapsed) / static_cast<float>(brew_temp_ramp_time_ms_));
          desired_temp = brew_target_temp_ + brew_temp_offset_ * frac;
        }
        (void)desired_temp;  // used in Phase 2 climate call
      }

      // Auto-terminate when flow_max ml reached
      float volume = brew_pump_ ? brew_pump_->get_flow_total() : 0.0f;
      if (volume >= brew_flow_max_ml_) {
        last_shot_time_s_ = static_cast<float>(millis() - brew_shot_start_ms_) / 1000.0f;
        last_shot_volume_ml_ = volume;
        ESP_LOGI(TAG, "Brew: DONE — volume=%.1fml  yield=%.1fml  time=%.1fs",
                 volume, volume - brew_flow_offset_ml_, last_shot_time_s_);
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
      // Phase 9: run cleanup_script here.
      // Phase 7 placeholder: transition to CLEANUP immediately.
      ESP_LOGI(TAG, "Brew: DONE → CLEANUP");
      brew_state_ = BrewState::CLEANUP;
      state_entered_ms_ = millis();
      break;

    case BrewState::CLEANUP:
      // Phase 9: cleanup_script runs here.
      // Phase 7 placeholder: return to idle immediately.
      ESP_LOGI(TAG, "Brew: CLEANUP → IDLE");
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
      // If a heater controller is wired, wait until the thermoblock reaches
      // steam temperature before opening the valve.  Without a controller the
      // machine transitions immediately (backward-compatible placeholder).
      if (steam_heater_ctrl_) {
        if (steam_heater_ctrl_->get_current_temperature() < steam_target_temp_) {
          break;  // Still heating — wait
        }
      }
      ESP_LOGI(TAG, "Steam: HEATING → STEAMING (%.1f°C)",
               steam_heater_ctrl_ ? steam_heater_ctrl_->get_current_temperature()
                                  : steam_target_temp_);
      steam_state_ = SteamState::STEAMING;
      state_entered_ms_ = millis();
      if (steam_valve_)
        steam_valve_->open();
      if (steam_pump_)
        steam_pump_->turn_on();
      break;

    case SteamState::STEAMING: {
      // Bang-bang flow rate control: toggle pump to maintain steam_flow_max_ml_per_s_.
      // Falls back to continuous pump operation when no flow meter is wired
      // (get_flow_rate() returns 0 by default, keeping the pump on).
      if (steam_pump_) {
        float current_rate = steam_pump_->get_flow_rate();
        if (current_rate < steam_flow_max_ml_per_s_) {
          if (!steam_pump_->is_running())
            steam_pump_->turn_on();
        } else {
          if (steam_pump_->is_running())
            steam_pump_->turn_off();
        }
      }
      break;
    }

    case SteamState::COOLING:
      // Purge valve was opened by steam_stop(); keep purging while the
      // thermoblock cools.  If a controller is wired, wait for the temperature
      // to drop to cool_down_to_ before proceeding to CLEANUP.
      if (steam_heater_ctrl_) {
        if (steam_heater_ctrl_->get_current_temperature() > steam_cool_down_to_) {
          break;  // Still cooling — wait
        }
      }
      ESP_LOGI(TAG, "Steam: COOLING → CLEANUP (%.1f°C)",
               steam_heater_ctrl_ ? steam_heater_ctrl_->get_current_temperature()
                                  : steam_cool_down_to_);
      steam_state_ = SteamState::CLEANUP;
      state_entered_ms_ = millis();
      break;

    case SteamState::CLEANUP:
      // Close purge valve and return to idle.
      ESP_LOGI(TAG, "Steam: CLEANUP → IDLE");
      if (steam_purge_valve_)
        steam_purge_valve_->close();
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
void EspressoMachine::enter_brewing_() {
  brew_state_ = BrewState::BREWING;
  brew_shot_start_ms_ = millis();
  // Reset flow counter so flow_max measures extraction volume only.
  // Called only when pre-infusion ran; without pre-infusion, flow was
  // reset at brew_start() and nothing has flowed during HEATING.
  if (pre_infusion_enabled_) {
    if (brew_pump_)
      brew_pump_->reset_flow();
  }
  if (brew_valve_)
    brew_valve_->open();
  if (brew_pump_)
    brew_pump_->turn_on();
  ESP_LOGI(TAG, "Brew: BREWING — target=%.1fml  temp_offset=%.1f°C",
           brew_flow_max_ml_, brew_temp_offset_);
}

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
