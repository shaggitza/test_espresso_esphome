#include "espresso_machine.h"
#include "esphome/core/log.h"

namespace esphome {
namespace espresso_machine {

static const char *const TAG = "espresso_machine";

// ---------------------------------------------------------------------------
// BrewFlowMaxNumber
// ---------------------------------------------------------------------------

void BrewFlowMaxNumber::control(float value) {
  if (parent_ != nullptr) {
    parent_->set_brew_flow_max(value);
    publish_state(value);
    ESP_LOGI(TAG, "Brew flow max updated to %.0f mL", value);
  }
}

// ---------------------------------------------------------------------------
// TempSurfSwitch
// ---------------------------------------------------------------------------

void TempSurfSwitch::write_state(bool state) {
  if (parent_ != nullptr) {
    parent_->set_temp_surf_enabled(state);
    publish_state(state);
    ESP_LOGI(TAG, "Temperature surfing %s", state ? "enabled" : "disabled");
  }
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void EspressoMachine::setup() {
  ESP_LOGI(TAG, "Espresso machine orchestrator initialised");
  ESP_LOGI(TAG, "  brew: target=%.1f°C  flow_max=%.1fml  offset=%.1fml",
           brew_target_temp_, brew_flow_max_ml_, brew_flow_offset_ml_);
  ESP_LOGI(TAG, "  steam: target=%.1f°C  cool_down_to=%.1f°C  flow_max=%.2fml/s",
           steam_target_temp_, steam_cool_down_to_, steam_flow_max_ml_per_s_);
  if (brew_flow_max_number_ != nullptr)
    brew_flow_max_number_->publish_state(brew_flow_max_ml_);
  if (temp_surf_switch_ != nullptr)
    temp_surf_switch_->publish_state(temp_surf_enabled_);
  safe_stop_all_();
  publish_status_();
}

// ---------------------------------------------------------------------------
// Main loop — advance whichever state machine is active
// ---------------------------------------------------------------------------
void EspressoMachine::loop() {
  if (check_over_temp_safety_())
    return;  // cutoff fired; do not advance state machines this tick

  switch (mode_) {
    case EspressoMode::BREWING:
      advance_brew_();
      break;
    case EspressoMode::STEAMING:
      advance_steam_();
      break;
    case EspressoMode::FLUSHING:
      advance_flush_();
      break;
    case EspressoMode::IDLE:
    default:
      break;
  }
  // Publish verbose status on every tick; deduplication in publish_status_()
  // ensures HA is only updated when the message actually changes.
  publish_status_();
}

// ---------------------------------------------------------------------------
// Power control
// ---------------------------------------------------------------------------
void EspressoMachine::machine_on() {
  if (powered_on_) {
    ESP_LOGD(TAG, "machine_on: already on");
    return;
  }
  powered_on_ = true;
  ESP_LOGI(TAG, "Machine ON");
}

void EspressoMachine::machine_off() {
  if (!powered_on_) {
    ESP_LOGD(TAG, "machine_off: already off");
    return;
  }
  ESP_LOGI(TAG, "Machine OFF");
  powered_on_ = false;

  switch (mode_) {
    case EspressoMode::BREWING:
      // Stop brew immediately — it is safe to interrupt at any point.
      ESP_LOGI(TAG, "Machine OFF: stopping active brew");
      safe_stop_all_();
      brew_state_ = BrewState::IDLE;
      mode_ = EspressoMode::IDLE;
      publish_status_();
      break;

    case EspressoMode::STEAMING:
      if (steam_state_ == SteamState::STEAMING || steam_state_ == SteamState::HEATING ||
          steam_state_ == SteamState::PURGING) {
        // Initiate cool-down + purge before shutting down (safety: prevents steam burns
        // if the user turns the machine off while steam pressure is still present).
        ESP_LOGI(TAG, "Machine OFF: initiating steam cool-down + purge sequence");
        steam_stop();  // enters COOLING (opens purge valve, lowers heater setpoint)
      } else {
        // Already in COOLING or CLEANUP — the state machine will reach IDLE on its own.
        ESP_LOGI(TAG, "Machine OFF: steam purge already in progress — completing before shutdown");
      }
      // Do NOT force mode_ = IDLE here; the state machine loop must complete the purge.
      break;

    case EspressoMode::FLUSHING:
      // Stop flush immediately — no pressure concern.
      ESP_LOGI(TAG, "Machine OFF: stopping active flush");
      safe_stop_all_();
      mode_ = EspressoMode::IDLE;
      publish_status_();
      break;

    case EspressoMode::IDLE:
    default:
      // Nothing active — machine hardware is already safe.
      break;
  }
}

// ---------------------------------------------------------------------------
// Public actions
// ---------------------------------------------------------------------------
void EspressoMachine::brew_start() {
  if (!powered_on_) {
    ESP_LOGW(TAG, "brew_start ignored: machine is off");
    return;
  }
  if (over_temp_cutoff_triggered_) {
    ESP_LOGW(TAG, "brew_start ignored: over-temperature safety cutoff is active — reboot required");
    return;
  }
  if (mode_ != EspressoMode::IDLE) {
    ESP_LOGW(TAG, "brew_start ignored: machine is %s", mode_name());
    return;
  }
  ESP_LOGI(TAG, "Brew START — target=%.1f°C  flow_max=%.1fml", brew_target_temp_, brew_flow_max_ml_);
  mode_ = EspressoMode::BREWING;
  brew_start_ms_ = millis();

  // Always close the brew valve and stop/reset the pump first.
  if (brew_valve_)
    brew_valve_->close();
  if (brew_pump_) {
    brew_pump_->turn_off();
    brew_pump_->reset_flow();
  }

  // If temperature_cooldown is enabled and the thermoblock is above the brew
  // target (e.g. still hot after an aborted steam session), cool it down first
  // before entering the HEATING/PRE_INFUSION/BREWING sequence.  This prevents
  // a shot from starting at the wrong temperature and ensures the PID has
  // settled at brew_target_temp_ before extraction begins.
  if (brew_temperature_cooldown_ && brew_heater_ctrl_ && brew_target_temp_ > 0.0f &&
      brew_heater_ctrl_->is_above_target(brew_target_temp_)) {
    ESP_LOGI(TAG, "Brew: START → COOLING (%.1f°C → %.1f°C first)",
             brew_heater_ctrl_->get_current_temperature(), brew_target_temp_);
    brew_heater_ctrl_->set_target_temperature(brew_target_temp_);
    if (brew_purge_valve_)
      brew_purge_valve_->open();
    if (brew_pump_) {
      brew_pump_->set_bypass_mode(true);
      brew_pump_->turn_on();
    }
    brew_state_ = BrewState::COOLING;
  } else {
    // Normal path: close purge valve and start waiting for brew temperature.
    if (brew_purge_valve_)
      brew_purge_valve_->close();
    brew_state_ = BrewState::HEATING;
    // NOTE: heater setpoint is raised to brew_target_temp_ when a brew heater
    // controller is wired via set_brew_heater_ctrl() (P1-2).
    if (brew_heater_ctrl_ && brew_target_temp_ > 0.0f)
      brew_heater_ctrl_->set_target_temperature(brew_target_temp_);
  }
  state_entered_ms_ = millis();
  publish_status_();
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
  publish_status_();
}

void EspressoMachine::steam_start() {
  if (!powered_on_) {
    ESP_LOGW(TAG, "steam_start ignored: machine is off");
    return;
  }
  if (over_temp_cutoff_triggered_) {
    ESP_LOGW(TAG, "steam_start ignored: over-temperature safety cutoff is active — reboot required");
    return;
  }
  if (mode_ != EspressoMode::IDLE) {
    ESP_LOGW(TAG, "steam_start ignored: machine is %s", mode_name());
    return;
  }
  ESP_LOGI(TAG, "Steam START — target=%.1f°C", steam_target_temp_);
  mode_ = EspressoMode::STEAMING;
  steam_state_ = SteamState::HEATING;
  state_entered_ms_ = millis();
  publish_status_();

  // Close all valves and stop pump while heating to steam temperature
  if (steam_valve_)
    steam_valve_->close();
  if (steam_purge_valve_)
    steam_purge_valve_->close();
  if (steam_pump_) {
    steam_pump_->turn_off();
    steam_pump_->reset_flow();  // reset for purge volume tracking
  }

  // Raise heater setpoint to steam temperature if a controller is wired
  if (steam_heater_ctrl_)
    steam_heater_ctrl_->set_target_temperature(steam_target_temp_);
}

void EspressoMachine::steam_stop() {
  if (mode_ != EspressoMode::STEAMING) {
    ESP_LOGW(TAG, "steam_stop ignored: machine is %s", mode_name());
    return;
  }
  if (steam_state_ == SteamState::HEATING || steam_state_ == SteamState::PURGING) {
    // Cancel before steaming began — lower setpoint and stop immediately.
    const char *cancel_phase = (steam_state_ == SteamState::HEATING) ? "heat-up" : "purge";
    ESP_LOGI(TAG, "Steam STOP (cancelled during %s)", cancel_phase);
    if (steam_heater_ctrl_)
      steam_heater_ctrl_->set_target_temperature(steam_cool_down_to_);
    safe_stop_all_();
    steam_state_ = SteamState::IDLE;
    mode_ = EspressoMode::IDLE;
    publish_status_();
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
  if (steam_pump_) {
    steam_pump_->set_target_flow(0.0f);  // disable pump-level flow control
    steam_pump_->turn_off();
  }
  steam_state_ = SteamState::COOLING;
  state_entered_ms_ = millis();
  // Lower heater setpoint to cool-down temperature
  if (steam_heater_ctrl_)
    steam_heater_ctrl_->set_target_temperature(steam_cool_down_to_);
  publish_status_();
}

void EspressoMachine::flush(float volume_ml) {
  if (!powered_on_) {
    ESP_LOGW(TAG, "flush ignored: machine is off");
    return;
  }
  if (over_temp_cutoff_triggered_) {
    ESP_LOGW(TAG, "flush ignored: over-temperature safety cutoff is active — reboot required");
    return;
  }
  if (mode_ != EspressoMode::IDLE) {
    ESP_LOGW(TAG, "flush ignored: machine is %s", mode_name());
    return;
  }
  if (volume_ml <= 0.0f) {
    ESP_LOGW(TAG, "flush ignored: volume_ml must be > 0");
    return;
  }
  ESP_LOGI(TAG, "Flush: pumping %.1fml through brew purge valve", volume_ml);
  flush_volume_ml_ = volume_ml;
  mode_ = EspressoMode::FLUSHING;
  if (brew_pump_) {
    brew_pump_->reset_flow();
    brew_pump_->set_bypass_mode(true);  // No puck resistance through purge valve
    brew_pump_->turn_on();
  }
  if (brew_purge_valve_)
    brew_purge_valve_->open();
  publish_status_();
}

// ---------------------------------------------------------------------------
// Brew state machine
// ---------------------------------------------------------------------------
void EspressoMachine::advance_brew_() {
  switch (brew_state_) {
    case BrewState::HEATING:
      // Gate transition on actual temperature when a heater controller is wired.
      // Without a controller, transition immediately (backward-compatible placeholder).
      if (brew_heater_ctrl_) {
        if (!brew_heater_ctrl_->is_ready(brew_target_temp_)) {
          break;  // Still heating — wait
        }
        ESP_LOGI(TAG, "Brew: HEATING → next (%.1f°C)",
                 brew_heater_ctrl_->get_current_temperature());
      }
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
        publish_status_();
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
      // Brew timeout (P0-4): stop the shot if it has been running too long.
      // This protects against a stuck flow sensor returning 0 indefinitely
      // (e.g. after a Wi-Fi disconnect prevents a manual stop from HA).
      if (brew_timeout_ms_ > 0 && (millis() - brew_start_ms_) >= brew_timeout_ms_) {
        ESP_LOGW(TAG, "Brew: TIMEOUT after %ums — stopping shot", brew_timeout_ms_);
        safe_stop_all_();
        brew_state_ = BrewState::IDLE;
        mode_ = EspressoMode::IDLE;
        publish_status_();
        break;
      }

      // Temperature surfing: linearly ramp the desired setpoint from
      // (target + offset) back to target over brew_temp_ramp_time_ms_.
      // Applied to the brew heater controller when wired (P1-3).
      // Only active when the temp_surf_enabled_ flag is true (HA switch).
      if (brew_heater_ctrl_ && temp_surf_enabled_ && brew_temp_offset_ > 0.0f && brew_temp_ramp_time_ms_ > 0) {
        uint32_t elapsed = millis() - brew_shot_start_ms_;
        float desired_temp;
        if (elapsed >= brew_temp_ramp_time_ms_) {
          desired_temp = brew_target_temp_;
        } else {
          float frac = 1.0f - (static_cast<float>(elapsed) / static_cast<float>(brew_temp_ramp_time_ms_));
          desired_temp = brew_target_temp_ + brew_temp_offset_ * frac;
        }
        brew_heater_ctrl_->set_target_temperature(desired_temp);
      }

      // Auto-terminate when flow_max ml reached
      float volume = brew_pump_ ? brew_pump_->get_flow_total() : 0.0f;
      if (volume >= brew_flow_max_ml_) {
        last_shot_time_s_ = static_cast<float>(millis() - brew_shot_start_ms_) / 1000.0f;
        last_shot_volume_ml_ = volume;
        ESP_LOGI(TAG, "Brew: DONE — volume=%.1fml  yield=%.1fml  time=%.1fs",
                 volume, volume - brew_flow_offset_ml_, last_shot_time_s_);
        // Publish shot stats to HA sensor entities (P1-5)
        if (last_shot_time_sensor_ != nullptr)
          last_shot_time_sensor_->publish_state(last_shot_time_s_);
        if (last_shot_volume_sensor_ != nullptr)
          last_shot_volume_sensor_->publish_state(last_shot_volume_ml_);
        if (last_shot_yield_sensor_ != nullptr)
          last_shot_yield_sensor_->publish_state(get_last_shot_yield_ml());
        brew_state_ = BrewState::DONE;
        state_entered_ms_ = millis();
        if (brew_pump_)
          brew_pump_->turn_off();
        if (brew_valve_)
          brew_valve_->close();
        publish_status_();
      }
      break;
    }

    case BrewState::DONE:
      // Run the user-configured cleanup script (P2-1), then enter CLEANUP.
      ESP_LOGI(TAG, "Brew: DONE → CLEANUP");
      if (brew_cleanup_fn_)
        brew_cleanup_fn_();
      brew_state_ = BrewState::CLEANUP;
      state_entered_ms_ = millis();
      publish_status_();
      break;

    case BrewState::COOLING:
      // Wait for the thermoblock to cool into the acceptable range around
      // brew_target_temp_.  is_ready() checks both directions (too cold AND
      // too hot), so this correctly waits until the block has settled within
      // temperature_tolerance_ of brew_target_temp_.
      // brew_heater_ctrl_ is guaranteed non-null when COOLING is entered.
      if (!brew_heater_ctrl_->is_ready(brew_target_temp_)) {
        break;  // Still out of range — wait
      }
      ESP_LOGI(TAG, "Brew: COOLING → HEATING (%.1f°C)",
               brew_heater_ctrl_->get_current_temperature());
      if (brew_pump_) {
        brew_pump_->turn_off();
        brew_pump_->set_bypass_mode(false);
        brew_pump_->reset_flow();
      }
      if (brew_purge_valve_)
        brew_purge_valve_->close();
      brew_state_ = BrewState::HEATING;
      state_entered_ms_ = millis();
      publish_status_();
      break;

    case BrewState::CLEANUP:
      // Phase 9 placeholder: return to idle immediately after cleanup.
      ESP_LOGI(TAG, "Brew: CLEANUP → IDLE");
      brew_state_ = BrewState::IDLE;
      mode_ = EspressoMode::IDLE;
      publish_status_();
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
        if (!steam_heater_ctrl_->is_ready(steam_target_temp_)) {
          break;  // Still heating — wait
        }
      }
      state_entered_ms_ = millis();
      if (steam_purge_volume_ml_ > 0.0f) {
        // Purge first: pump water through the purge valve to clear the steam
        // path before opening the steam valve.  This ensures only dry steam
        // reaches the wand, not residual water from the thermoblock.
        ESP_LOGI(TAG, "Steam: HEATING → PURGING (%.1f°C) — clearing %.1fml through purge valve",
                 steam_heater_ctrl_ ? steam_heater_ctrl_->get_current_temperature()
                                    : steam_target_temp_,
                 steam_purge_volume_ml_);
        steam_state_ = SteamState::PURGING;
        if (steam_pump_) {
          steam_pump_->reset_flow();
          steam_pump_->set_bypass_mode(true);  // No puck resistance through valve
          steam_pump_->turn_on();
        }
        if (steam_purge_valve_)
          steam_purge_valve_->open();
        publish_status_();
      } else {
        // No purge configured: open steam valve immediately (backward compat).
        ESP_LOGI(TAG, "Steam: HEATING → STEAMING (%.1f°C)",
                 steam_heater_ctrl_ ? steam_heater_ctrl_->get_current_temperature()
                                    : steam_target_temp_);
        steam_state_ = SteamState::STEAMING;
        steam_start_ms_ = millis();
        if (steam_valve_)
          steam_valve_->open();
        if (steam_pump_) {
          steam_pump_->set_bypass_mode(true);  // No puck resistance through valve
          // Delegate bang-bang flow control to the pump (P2-7).
          // The pump's loop() will modulate on/off to maintain this rate,
          // respecting its own min_on/min_off timing constraints.
          steam_pump_->set_target_flow(steam_flow_max_ml_per_s_);
          steam_pump_->turn_on();
        }
        publish_status_();
      }
      break;

    case SteamState::PURGING: {
      // Pump water through the purge valve until the target purge volume is
      // reached, then close the purge valve and open the steam valve.
      float purged = steam_pump_ ? steam_pump_->get_flow_total() : 0.0f;
      if (purged >= steam_purge_volume_ml_) {
        ESP_LOGI(TAG, "Steam: PURGING → STEAMING (purged %.1fml)", purged);
        if (steam_purge_valve_)
          steam_purge_valve_->close();
        if (steam_pump_)
          steam_pump_->reset_flow();  // reset so STEAMING tracks steam-only volume
        // Stay in bypass mode — still pumping through steam valve.
        // Delegate bang-bang flow control to the pump (P2-7).
        if (steam_pump_)
          steam_pump_->set_target_flow(steam_flow_max_ml_per_s_);
        steam_state_ = SteamState::STEAMING;
        steam_start_ms_ = millis();
        state_entered_ms_ = millis();
        if (steam_valve_)
          steam_valve_->open();
        // pump continues running for steaming
        publish_status_();
      }
      break;
    }

    case SteamState::STEAMING: {
      // Auto-stop: safety timeout (e.g. Wi-Fi disconnect prevents manual stop).
      if (steam_timeout_ms_ > 0 && (millis() - steam_start_ms_) >= steam_timeout_ms_) {
        ESP_LOGW(TAG, "Steam: TIMEOUT after %ums — stopping", steam_timeout_ms_);
        steam_stop();
        break;
      }
      // Bang-bang flow rate control (P2-7) is now delegated to the pump.
      // When entering STEAMING, the orchestrator called
      //   steam_pump_->set_target_flow(steam_flow_max_ml_per_s_)
      // and the pump's own loop() modulates on/off to maintain that rate,
      // honouring its pump_min_on_time / pump_min_off_time constraints.
      // Falls back to continuous operation when no flow meter is wired
      // (pump reports rate=0 < target, keeping itself on).
      break;
    }

    case SteamState::COOLING:
      // Purge valve was opened by steam_stop(); run the pump to actively
      // cool the thermoblock by pushing cold water through.  The pump is
      // in bypass mode (no puck resistance) so flow is maximum.
      if (steam_pump_) {
        if (!steam_pump_->is_running()) {
          steam_pump_->set_bypass_mode(true);
          steam_pump_->turn_on();
        }
      }
      // Wait for temperature to drop to cool_down_to_ before proceeding.
      if (steam_heater_ctrl_) {
        if (steam_heater_ctrl_->get_current_temperature() > steam_cool_down_to_) {
          break;  // Still cooling — wait
        }
      }
      ESP_LOGI(TAG, "Steam: COOLING → CLEANUP (%.1f°C)",
               steam_heater_ctrl_ ? steam_heater_ctrl_->get_current_temperature()
                                  : steam_cool_down_to_);
      // Stop pump and exit bypass mode before moving to CLEANUP
      if (steam_pump_) {
        steam_pump_->turn_off();
        steam_pump_->set_bypass_mode(false);
      }
      steam_state_ = SteamState::CLEANUP;
      state_entered_ms_ = millis();
      publish_status_();
      break;

    case SteamState::CLEANUP:
      // Run the user-configured cleanup script (P2-1), close purge valve and return to idle.
      ESP_LOGI(TAG, "Steam: CLEANUP → IDLE");
      if (steam_cleanup_fn_)
        steam_cleanup_fn_();
      if (steam_purge_valve_)
        steam_purge_valve_->close();
      steam_state_ = SteamState::IDLE;
      mode_ = EspressoMode::IDLE;
      publish_status_();
      break;

    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Flush state machine (P2-2)
// ---------------------------------------------------------------------------
void EspressoMachine::advance_flush_() {
  float pumped = brew_pump_ ? brew_pump_->get_flow_total() : 0.0f;
  if (pumped >= flush_volume_ml_) {
    ESP_LOGI(TAG, "Flush: DONE — pumped %.1fml through brew purge valve", pumped);
    if (brew_pump_) {
      brew_pump_->turn_off();
      brew_pump_->set_bypass_mode(false);
    }
    if (brew_purge_valve_)
      brew_purge_valve_->close();
    mode_ = EspressoMode::IDLE;
    publish_status_();
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
  publish_status_();
}

void EspressoMachine::safe_stop_all_() {
  if (brew_pump_) {
    brew_pump_->set_target_flow(0.0f);
    brew_pump_->turn_off();
    brew_pump_->set_bypass_mode(false);
  }
  if (steam_pump_) {
    steam_pump_->set_target_flow(0.0f);
    steam_pump_->turn_off();
    steam_pump_->set_bypass_mode(false);
  }
  if (brew_valve_)
    brew_valve_->close();
  if (brew_purge_valve_)
    brew_purge_valve_->close();
  if (steam_valve_)
    steam_valve_->close();
  if (steam_purge_valve_)
    steam_purge_valve_->close();
}

// ---------------------------------------------------------------------------
// Safety: hard over-temperature cutoff (P0-1) and sensor NaN fault (P0-3)
// ---------------------------------------------------------------------------
bool EspressoMachine::check_over_temp_safety_() {
  if (over_temp_sensor_ == nullptr)
    return false;

  // Already triggered — block all state machine activity until reboot.
  if (over_temp_cutoff_triggered_)
    return true;

  float temp = over_temp_sensor_->get_current_temperature();

  // P0-3: sensor fault — NaN means the thermocouple is disconnected or the
  // ADC returned a stuck value.  The PID would drive to 100% duty because it
  // computes a huge positive error.  Force the heater off immediately.
  if (std::isnan(temp)) {
    ESP_LOGE(TAG, "SAFETY: temperature sensor fault (NaN) — forcing heater OFF");
    over_temp_cutoff_triggered_ = true;
    safe_stop_all_();
    over_temp_sensor_->force_off();
    return true;
  }

  // P0-1: hard over-temperature cutoff — temperature exceeds the configured
  // limit.  Latch the flag so the heater cannot be re-enabled by the PID
  // until the device is rebooted.
  if (temp >= over_temp_limit_) {
    ESP_LOGE(TAG, "SAFETY: over-temperature cutoff at %.1f°C (limit %.1f°C) — forcing heater OFF",
             temp, over_temp_limit_);
    over_temp_cutoff_triggered_ = true;
    safe_stop_all_();
    over_temp_sensor_->force_off();
    return true;
  }

  return false;
}

const char *EspressoMachine::mode_name() const {
  switch (mode_) {
    case EspressoMode::IDLE:
      return "idle";
    case EspressoMode::BREWING:
      return "brewing";
    case EspressoMode::STEAMING:
      return "steaming";
    case EspressoMode::FLUSHING:
      return "flushing";
    default:
      return "unknown";
  }
}

std::string EspressoMachine::status_name() const {
  // Maximum status string length: "Heating to steam 135.0°C (now 165.0°C)"
  // is ~41 bytes; 128 bytes is a generous upper bound.
  static constexpr size_t STATUS_BUF_SIZE = 128;
  char buf[STATUS_BUF_SIZE];
  int n = 0;
  switch (mode_) {
    case EspressoMode::IDLE:
      return "Idle";

    case EspressoMode::FLUSHING: {
      float pumped = brew_pump_ ? brew_pump_->get_flow_total() : 0.0f;
      n = snprintf(buf, STATUS_BUF_SIZE, "Flushing: %.1f ml / %.1f ml", pumped, flush_volume_ml_);
      break;
    }

    case EspressoMode::BREWING:
      switch (brew_state_) {
        case BrewState::HEATING:
          if (brew_heater_ctrl_) {
            n = snprintf(buf, STATUS_BUF_SIZE, "Heating to %.1f°C (now %.1f°C)",
                         brew_target_temp_, brew_heater_ctrl_->get_current_temperature());
          } else {
            n = snprintf(buf, STATUS_BUF_SIZE, "Heating to %.1f°C", brew_target_temp_);
          }
          break;
        case BrewState::PRE_INFUSION: {
          float vol = brew_pump_ ? brew_pump_->get_flow_total() : 0.0f;
          n = snprintf(buf, STATUS_BUF_SIZE, "Pre-infusion: %.1f ml / %.1f ml", vol, pre_infusion_volume_ml_);
          break;
        }
        case BrewState::BREWING: {
          float vol = brew_pump_ ? brew_pump_->get_flow_total() : 0.0f;
          n = snprintf(buf, STATUS_BUF_SIZE, "Brewing: %.1f ml / %.1f ml", vol, brew_flow_max_ml_);
          break;
        }
        case BrewState::DONE:
          n = snprintf(buf, STATUS_BUF_SIZE, "Shot done: %.1f ml in %.1f s",
                       last_shot_volume_ml_, last_shot_time_s_);
          break;
        case BrewState::COOLING:
          if (brew_heater_ctrl_) {
            n = snprintf(buf, STATUS_BUF_SIZE, "Brew cooldown: %.1f°C → %.1f°C",
                         brew_heater_ctrl_->get_current_temperature(), brew_target_temp_);
          } else {
            n = snprintf(buf, STATUS_BUF_SIZE, "Brew cooldown to %.1f°C", brew_target_temp_);
          }
          break;
        case BrewState::CLEANUP:
          return "Brew cleanup";
        default:
          return "Brewing";
      }
      break;

    case EspressoMode::STEAMING:
      switch (steam_state_) {
        case SteamState::HEATING:
          if (steam_heater_ctrl_) {
            n = snprintf(buf, STATUS_BUF_SIZE, "Heating to steam %.1f°C (now %.1f°C)",
                         steam_target_temp_, steam_heater_ctrl_->get_current_temperature());
          } else {
            n = snprintf(buf, STATUS_BUF_SIZE, "Heating to steam %.1f°C", steam_target_temp_);
          }
          break;
        case SteamState::PURGING: {
          float purged = steam_pump_ ? steam_pump_->get_flow_total() : 0.0f;
          n = snprintf(buf, STATUS_BUF_SIZE, "Purging: %.1f ml / %.1f ml", purged, steam_purge_volume_ml_);
          break;
        }
        case SteamState::STEAMING: {
          float rate = steam_pump_ ? steam_pump_->get_flow_rate() : 0.0f;
          n = snprintf(buf, STATUS_BUF_SIZE, "Steaming: %.1f ml/s", rate);
          break;
        }
        case SteamState::COOLING:
          if (steam_heater_ctrl_) {
            n = snprintf(buf, STATUS_BUF_SIZE, "Cooling to %.1f°C (now %.1f°C)",
                         steam_cool_down_to_, steam_heater_ctrl_->get_current_temperature());
          } else {
            n = snprintf(buf, STATUS_BUF_SIZE, "Cooling to %.1f°C", steam_cool_down_to_);
          }
          break;
        case SteamState::CLEANUP:
          return "Steam cleanup";
        default:
          return "Steaming";
      }
      break;

    default:
      return "Idle";
  }

  if (n < 0 || static_cast<size_t>(n) >= STATUS_BUF_SIZE)
    ESP_LOGW(TAG, "status_name: output truncated (%d bytes)", n);
  return buf;
}

void EspressoMachine::publish_status_() {
  if (status_sensor_ == nullptr)
    return;
  std::string current = status_name();
  if (current != last_published_status_) {
    last_published_status_ = current;
    status_sensor_->publish_state(current);
  }
}

}  // namespace espresso_machine
}  // namespace esphome
