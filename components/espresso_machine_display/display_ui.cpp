#include "display_ui.h"
#include <cstdio>
#include <cstring>
#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace esphome {
namespace espresso_machine_display {

static const char *const TAG = "espresso_machine_display";

// ---------------------------------------------------------------------------
// Convenience macros for display colors on monochrome ST7920.
// On ESPHome monochrome displays COLOR_ON = "lit pixel" (white on black).
// ---------------------------------------------------------------------------
#define CLR_ON  display::COLOR_ON
#define CLR_OFF display::COLOR_OFF

// ============================================================================
// Setup
// ============================================================================

void EspressoMachineDisplay::setup() {
  ESP_LOGI(TAG, "EspressoMachineDisplay initializing");

  // Attempt interface casts for optional component pointers.
  // The concrete types (EspressoMachineHeater, FlowMeter) both implement
  // IHeater / IFlowMeter. dynamic_cast isn't available in the Arduino env,
  // so we use a compile-time static_cast — the component author is responsible
  // for passing the right types in YAML.
  if (heater_component_ != nullptr) {
    heater_ = static_cast<espresso_machine::IHeater *>(heater_component_);
  }
  if (flow_meter_component_ != nullptr) {
    flow_meter_ = static_cast<espresso_machine::IFlowMeter *>(flow_meter_component_);
  }

  // Mirror initial pre-infusion values from machine if available.
  // (These are already set from YAML; we track them locally for display.)
  if (machine_ != nullptr) {
    pi_enabled_ = machine_->get_pre_infusion_enabled();
    pi_volume_ml_ = machine_->get_pre_infusion_volume_ml();
    pi_hold_s_ = static_cast<float>(machine_->get_pre_infusion_hold_time_ms()) / 1000.0f;
  }

  last_input_ms_ = millis();
  needs_redraw_ = true;
}

// ============================================================================
// Loop
// ============================================================================

void EspressoMachineDisplay::loop() {
  handle_input_();

  // Track mode transitions for elapsed-time start and last shot volume.
  if (machine_ != nullptr) {
    bool brewing = (machine_->get_mode() == espresso_machine::EspressoMode::BREWING);
    bool steaming = (machine_->get_mode() == espresso_machine::EspressoMode::STEAMING);
    bool grinding = grinder_ != nullptr && grinder_->is_grinding();

    if (brewing && !was_brewing_) {
      brew_start_ms_ = millis();
      needs_redraw_ = true;
    }
    if (!brewing && was_brewing_) {
      // Capture last shot volume on completion.
      last_shot_volume_ = machine_->get_last_shot_volume_ml();
      needs_redraw_ = true;
    }
    if (steaming && !was_steaming_) {
      steam_start_ms_ = millis();
      needs_redraw_ = true;
    }
    if (!steaming && was_steaming_) {
      needs_redraw_ = true;
    }
    if (grinding && !was_grinding_) {
      grind_start_ms_ = millis();
      needs_redraw_ = true;
    }
    if (!grinding && was_grinding_) {
      needs_redraw_ = true;
    }

    was_brewing_ = brewing;
    was_steaming_ = steaming;
    was_grinding_ = grinding;
  }

  // On live-data screens refresh every 500 ms even without user input.
  bool live_screen = (screen_ == Screen::HOME &&
                      machine_ != nullptr &&
                      machine_->get_mode() != espresso_machine::EspressoMode::IDLE);
  if (live_screen && millis() - last_live_refresh_ms_ > 500) {
    last_live_refresh_ms_ = millis();
    needs_redraw_ = true;
  }

  // Edit-mode blink toggle every 500 ms.
  if (screen_ == Screen::EDIT_NUMBER) {
    if (millis() - edit_blink_ms_ > 500) {
      edit_blink_ms_ = millis();
      edit_blink_on_ = !edit_blink_on_;
      needs_redraw_ = true;
    }
  }

  // Error overlay expiry.
  if (screen_ == Screen::ERROR_OVERLAY && millis() > error_until_ms_) {
    go_to_(error_return_screen_);
  }
}

// ============================================================================
// Public render entry point (called from display lambda)
// ============================================================================

void EspressoMachineDisplay::render(display::DisplayBuffer &it) {
  if (!needs_redraw_)
    return;
  needs_redraw_ = false;

  it.clear();

  if (screensaver_active_) {
    // Blank screen — just return after clearing.
    return;
  }

  switch (screen_) {
    case Screen::HOME:
      render_home_(it);
      break;
    case Screen::BREW_MENU:
      render_brew_menu_(it);
      break;
    case Screen::STEAM_MENU:
      render_steam_menu_(it);
      break;
    case Screen::GRINDER_MENU:
      render_grinder_menu_(it);
      break;
    case Screen::SETTINGS_MENU:
      render_settings_menu_(it);
      break;
    case Screen::SETTINGS_MENU_P2:
      render_settings_menu_p2_(it);
      break;
    case Screen::MAINTENANCE_MENU:
      render_maintenance_menu_(it);
      break;
    case Screen::PRE_INFUSION_MENU:
      render_pre_infusion_menu_(it);
      break;
    case Screen::EDIT_NUMBER:
      render_edit_number_(it);
      break;
    case Screen::CONFIRM_DIALOG:
      render_confirm_dialog_(it);
      break;
    case Screen::ERROR_OVERLAY:
      render_error_overlay_(it);
      break;
  }
}

// ============================================================================
// Input handling
// ============================================================================

void EspressoMachineDisplay::set_encoder(sensor::Sensor *s) {
  encoder_ = s;
  if (s != nullptr) {
    s->add_on_state_callback([this](float val) {
      int delta = static_cast<int>(val - last_encoder_val_);
      last_encoder_val_ = val;
      if (delta != 0) {
        wake_screensaver_();
        handle_encoder_delta_(delta);
      }
    });
  }
}

void EspressoMachineDisplay::set_button(binary_sensor::BinarySensor *b) {
  button_ = b;
  if (b != nullptr) {
    b->add_on_state_callback([this](bool pressed) {
      wake_screensaver_();
      if (pressed) {
        button_was_pressed_ = true;
        button_press_ms_ = millis();
        long_press_fired_ = false;
      } else {
        if (button_was_pressed_ && !long_press_fired_) {
          click_pending_ = true;
        }
        button_was_pressed_ = false;
      }
    });
  }
}

void EspressoMachineDisplay::handle_input_() {
  // Screensaver check.
  if (!screensaver_active_ && screensaver_timeout_ms_ > 0 &&
      millis() - last_input_ms_ > screensaver_timeout_ms_) {
    screensaver_active_ = true;
    needs_redraw_ = true;
    return;
  }

  // Long press detection (polled every loop tick).
  if (button_was_pressed_ && !long_press_fired_ &&
      millis() - button_press_ms_ >= long_press_ms_) {
    long_press_fired_ = true;
    handle_long_press_();
    return;
  }

  if (click_pending_) {
    click_pending_ = false;
    handle_click_();
  }
}

void EspressoMachineDisplay::handle_encoder_delta_(int delta) {
  last_input_ms_ = millis();
  needs_redraw_ = true;

  switch (screen_) {
    case Screen::EDIT_NUMBER: {
      // Acceleration: use fast_step after 1 s of continuous rotation.
      uint32_t now = millis();
      if (!edit_ctx_.rotating) {
        edit_ctx_.rotating = true;
        edit_ctx_.rotation_start_ms = now;
      }
      float step = (now - edit_ctx_.rotation_start_ms > 1000)
                       ? edit_ctx_.fast_step
                       : edit_ctx_.step;
      edit_ctx_.value += delta * step;
      if (edit_ctx_.value < edit_ctx_.min_val) edit_ctx_.value = edit_ctx_.min_val;
      if (edit_ctx_.value > edit_ctx_.max_val) edit_ctx_.value = edit_ctx_.max_val;
      break;
    }
    case Screen::CONFIRM_DIALOG:
      // Rotate moves between YES and NO.
      confirm_ctx_.cursor_yes = (delta < 0);  // CW = NO, CCW = YES
      break;
    case Screen::HOME:
    case Screen::BREW_MENU:
    case Screen::STEAM_MENU:
    case Screen::GRINDER_MENU:
    case Screen::SETTINGS_MENU:
    case Screen::SETTINGS_MENU_P2:
    case Screen::MAINTENANCE_MENU:
    case Screen::PRE_INFUSION_MENU: {
      // Move cursor; clamp per-screen in each render function.
      cursor_ += delta;
      if (cursor_ < 0) cursor_ = 0;
      break;
    }
    default:
      break;
  }
}

void EspressoMachineDisplay::handle_click_() {
  last_input_ms_ = millis();
  needs_redraw_ = true;
  beep_();

  switch (screen_) {
    // ── CONFIRM DIALOG ──────────────────────────────────────────────────────
    case Screen::CONFIRM_DIALOG:
      if (confirm_ctx_.cursor_yes && confirm_ctx_.on_yes) {
        go_to_(confirm_ctx_.return_screen);
        confirm_ctx_.on_yes(confirm_ctx_.on_yes_ctx);
      } else {
        go_to_(confirm_ctx_.return_screen);
      }
      break;

    // ── NUMBER EDIT ─────────────────────────────────────────────────────────
    case Screen::EDIT_NUMBER:
      if (edit_ctx_.apply_fn) {
        edit_ctx_.apply_fn(edit_ctx_.value, edit_ctx_.apply_ctx);
      }
      go_to_(edit_ctx_.return_screen);
      break;

    // ── HOME SCREEN ─────────────────────────────────────────────────────────
    case Screen::HOME: {
      // Build ordered list of visible home actions.
      std::vector<std::string> visible;
      for (const auto &a : home_actions_) {
        if (a == "brew" && machine_ != nullptr) visible.push_back(a);
        else if (a == "steam" && machine_ != nullptr) visible.push_back(a);
        else if (a == "grind" && grinder_ != nullptr) visible.push_back(a);
        else if (a == "settings") visible.push_back(a);
        else if (a == "power" && machine_ != nullptr) visible.push_back(a);
      }
      int max_cur = static_cast<int>(visible.size()) - 1;
      if (cursor_ > max_cur) cursor_ = max_cur;
      if (cursor_ < 0) break;

      const std::string &sel = visible[static_cast<size_t>(cursor_)];
      if (sel == "brew")
        go_to_(Screen::BREW_MENU, 0);
      else if (sel == "steam")
        go_to_(Screen::STEAM_MENU, 0);
      else if (sel == "grind")
        go_to_(Screen::GRINDER_MENU, 0);
      else if (sel == "settings")
        go_to_(Screen::SETTINGS_MENU, 0);
      else if (sel == "power")
        action_power_toggle_();
      break;
    }

    // ── BREW MENU ───────────────────────────────────────────────────────────
    case Screen::BREW_MENU:
      switch (cursor_) {
        case 0: action_brew_start_(); break;
        case 1:  // Brew Temp
          if (heater_ != nullptr) {
            begin_edit_("BREW TEMP", "\xb0""C",
                        machine_->get_brew_target_temp(), 70.0f, 100.0f,
                        0.5f, 2.0f, Screen::BREW_MENU,
                        [](float v, void *ctx) {
                          auto *self = static_cast<EspressoMachineDisplay *>(ctx);
                          self->machine_->set_brew_target_temperature(v);
                        }, this);
          }
          break;
        case 2:  // Flow Max
          begin_edit_("FLOW MAX", "ml",
                      machine_->get_brew_flow_max(), 10.0f, 100.0f,
                      0.5f, 2.0f, Screen::BREW_MENU,
                      [](float v, void *ctx) {
                        auto *self = static_cast<EspressoMachineDisplay *>(ctx);
                        self->machine_->set_brew_flow_max(v);
                        if (self->machine_->get_brew_flow_max_number() != nullptr)
                          self->machine_->get_brew_flow_max_number()->publish_state(v);
                      }, this);
          break;
        case 3:  // Pre-infusion sub-menu
          go_to_(Screen::PRE_INFUSION_MENU, 0);
          break;
        case 4:  // Temp Surfing toggle
          if (machine_->get_temp_surf_switch() != nullptr) {
            auto *sw = machine_->get_temp_surf_switch();
            sw->toggle();
          }
          break;
        case 5:  // Cooldown toggle
          machine_->set_brew_temperature_cooldown(!machine_->get_brew_temperature_cooldown());
          break;
      }
      break;

    // ── STEAM MENU ──────────────────────────────────────────────────────────
    case Screen::STEAM_MENU:
      switch (cursor_) {
        case 0: action_steam_start_(); break;
        case 1: action_steam_stop_(); break;
        case 2:  // Steam Temp
          begin_edit_("STEAM TEMP", "\xb0""C",
                      machine_->get_steam_target_temp(), 110.0f, 155.0f,
                      0.5f, 2.0f, Screen::STEAM_MENU,
                      [](float v, void *ctx) {
                        auto *self = static_cast<EspressoMachineDisplay *>(ctx);
                        self->machine_->set_steam_target_temperature(v);
                      }, this);
          break;
        case 3:  // Cool To
          begin_edit_("COOL DOWN TO", "\xb0""C",
                      machine_->get_steam_cool_down_to(), 70.0f, 100.0f,
                      0.5f, 2.0f, Screen::STEAM_MENU,
                      [](float v, void *ctx) {
                        auto *self = static_cast<EspressoMachineDisplay *>(ctx);
                        self->machine_->set_steam_cool_down_to(v);
                      }, this);
          break;
        case 4:  // Purge Vol
          begin_edit_("PURGE VOL", "ml",
                      machine_->get_steam_purge_volume_ml(), 0.0f, 20.0f,
                      0.5f, 2.0f, Screen::STEAM_MENU,
                      [](float v, void *ctx) {
                        auto *self = static_cast<EspressoMachineDisplay *>(ctx);
                        self->machine_->set_steam_purge_volume_ml(v);
                      }, this);
          break;
        case 5:  // Flow Rate
          begin_edit_("STEAM RATE", "ml/s",
                      machine_->get_steam_flow_max(), 0.0f, 5.0f,
                      0.1f, 0.5f, Screen::STEAM_MENU,
                      [](float v, void *ctx) {
                        auto *self = static_cast<EspressoMachineDisplay *>(ctx);
                        self->machine_->set_steam_flow_max(v);
                      }, this);
          break;
        case 6:  // Timeout
          begin_edit_("STEAM TIMEOUT", "min",
                      static_cast<float>(machine_->get_steam_timeout_ms()) / 60000.0f,
                      0.0f, 15.0f, 1.0f, 5.0f, Screen::STEAM_MENU,
                      [](float v, void *ctx) {
                        auto *self = static_cast<EspressoMachineDisplay *>(ctx);
                        self->machine_->set_steam_timeout_ms(
                            static_cast<uint32_t>(v * 60000.0f));
                      }, this);
          break;
      }
      break;

    // ── GRINDER MENU ────────────────────────────────────────────────────────
    case Screen::GRINDER_MENU:
      if (grinder_ == nullptr) break;
      switch (cursor_) {
        case 0: action_grind_start_(); break;
        case 1:  // Grind Time
          begin_edit_("GRIND TIME", "s",
                      static_cast<float>(grinder_->get_current_grind_time_ms()) / 1000.0f,
                      0.5f, 30.0f, 0.5f, 2.0f, Screen::GRINDER_MENU,
                      [](float v, void *ctx) {
                        auto *self = static_cast<EspressoMachineDisplay *>(ctx);
                        self->grinder_->set_default_grind_time(
                            static_cast<uint32_t>(v * 1000.0f));
                        if (self->grinder_->get_grind_time_number() != nullptr)
                          self->grinder_->get_grind_time_number()->publish_state(v * 1000.0f);
                      }, this);
          break;
      }
      break;

    // ── SETTINGS MENU ───────────────────────────────────────────────────────
    case Screen::SETTINGS_MENU:
      switch (cursor_) {
        case 0:  // Brew Temp
          begin_edit_("BREW TEMP", "\xb0""C",
                      machine_->get_brew_target_temp(), 70.0f, 100.0f,
                      0.5f, 2.0f, Screen::SETTINGS_MENU,
                      [](float v, void *ctx) {
                        auto *self = static_cast<EspressoMachineDisplay *>(ctx);
                        self->machine_->set_brew_target_temperature(v);
                      }, this);
          break;
        case 1:  // Steam Temp
          begin_edit_("STEAM TEMP", "\xb0""C",
                      machine_->get_steam_target_temp(), 110.0f, 155.0f,
                      0.5f, 2.0f, Screen::SETTINGS_MENU,
                      [](float v, void *ctx) {
                        auto *self = static_cast<EspressoMachineDisplay *>(ctx);
                        self->machine_->set_steam_target_temperature(v);
                      }, this);
          break;
        case 2:  // Flow Max
          begin_edit_("FLOW MAX", "ml",
                      machine_->get_brew_flow_max(), 10.0f, 100.0f,
                      0.5f, 2.0f, Screen::SETTINGS_MENU,
                      [](float v, void *ctx) {
                        auto *self = static_cast<EspressoMachineDisplay *>(ctx);
                        self->machine_->set_brew_flow_max(v);
                        if (self->machine_->get_brew_flow_max_number() != nullptr)
                          self->machine_->get_brew_flow_max_number()->publish_state(v);
                      }, this);
          break;
        case 3:  // Flow Offset
          begin_edit_("FLOW OFFSET", "ml",
                      machine_->get_brew_flow_offset(), 0.0f, 40.0f,
                      0.5f, 2.0f, Screen::SETTINGS_MENU,
                      [](float v, void *ctx) {
                        auto *self = static_cast<EspressoMachineDisplay *>(ctx);
                        self->machine_->set_brew_flow_offset(v);
                      }, this);
          break;
        case 4:  // Grind Time
          if (grinder_ != nullptr) {
            begin_edit_("GRIND TIME", "s",
                        static_cast<float>(grinder_->get_current_grind_time_ms()) / 1000.0f,
                        0.5f, 30.0f, 0.5f, 2.0f, Screen::SETTINGS_MENU,
                        [](float v, void *ctx) {
                          auto *self = static_cast<EspressoMachineDisplay *>(ctx);
                          self->grinder_->set_default_grind_time(
                              static_cast<uint32_t>(v * 1000.0f));
                          if (self->grinder_->get_grind_time_number() != nullptr)
                            self->grinder_->get_grind_time_number()->publish_state(
                                v * 1000.0f);
                        }, this);
          }
          break;
        case 5:  // Pre-infusion sub-menu
          go_to_(Screen::PRE_INFUSION_MENU, 0);
          break;
        case 6:  // more... → page 2
          go_to_(Screen::SETTINGS_MENU_P2, 0);
          break;
      }
      break;

    // ── SETTINGS PAGE 2 ─────────────────────────────────────────────────────
    case Screen::SETTINGS_MENU_P2:
      switch (cursor_) {
        case 0:  // Temp Surfing toggle
          if (machine_->get_temp_surf_switch() != nullptr)
            machine_->get_temp_surf_switch()->toggle();
          break;
        case 1:  // Temp Cooldown toggle
          machine_->set_brew_temperature_cooldown(!machine_->get_brew_temperature_cooldown());
          break;
        case 2:  // Idle Timeout (minutes)
          begin_edit_("IDLE TIMEOUT", "min",
                      static_cast<float>(machine_->get_idle_timeout_ms()) / 60000.0f,
                      0.0f, 120.0f, 1.0f, 5.0f, Screen::SETTINGS_MENU_P2,
                      [](float v, void *ctx) {
                        auto *self = static_cast<EspressoMachineDisplay *>(ctx);
                        self->machine_->set_idle_timeout_ms(
                            static_cast<uint32_t>(v * 60000.0f));
                      }, this);
          break;
        case 3:  // Maintenance sub-menu
          go_to_(Screen::MAINTENANCE_MENU, 0);
          break;
      }
      break;

    // ── MAINTENANCE MENU ────────────────────────────────────────────────────
    case Screen::MAINTENANCE_MENU:
      switch (cursor_) {
        case 0:  // Flush N ml (confirm first)
          begin_confirm_("Flush?", Screen::MAINTENANCE_MENU,
                         [](void *ctx) {
                           auto *self = static_cast<EspressoMachineDisplay *>(ctx);
                           self->action_flush_(self->flush_vol_ml_);
                         }, this);
          break;
        case 1:  // Flush Volume
          begin_edit_("FLUSH VOL", "ml",
                      flush_vol_ml_, 10.0f, 200.0f,
                      5.0f, 20.0f, Screen::MAINTENANCE_MENU,
                      [](float v, void *ctx) {
                        auto *self = static_cast<EspressoMachineDisplay *>(ctx);
                        self->flush_vol_ml_ = v;
                      }, this);
          break;
        case 2:  // Restart
          begin_confirm_("Restart ESP?", Screen::MAINTENANCE_MENU,
                         [](void * /*ctx*/) { App.safe_reboot(); }, nullptr);
          break;
      }
      break;

    // ── PRE-INFUSION MENU ───────────────────────────────────────────────────
    case Screen::PRE_INFUSION_MENU:
      switch (cursor_) {
        case 0:  // Enabled toggle
          pi_enabled_ = !pi_enabled_;
          machine_->set_pre_infusion_enabled(pi_enabled_);
          break;
        case 1:  // Volume
          begin_edit_("PRE-INF VOL", "ml",
                      pi_volume_ml_, 1.0f, 30.0f, 0.5f, 2.0f,
                      Screen::PRE_INFUSION_MENU,
                      [](float v, void *ctx) {
                        auto *self = static_cast<EspressoMachineDisplay *>(ctx);
                        self->pi_volume_ml_ = v;
                        self->machine_->set_pre_infusion_volume_ml(v);
                      }, this);
          break;
        case 2:  // Hold Time
          begin_edit_("PRE-INF HOLD", "s",
                      pi_hold_s_, 0.0f, 30.0f, 0.5f, 2.0f,
                      Screen::PRE_INFUSION_MENU,
                      [](float v, void *ctx) {
                        auto *self = static_cast<EspressoMachineDisplay *>(ctx);
                        self->pi_hold_s_ = v;
                        self->machine_->set_pre_infusion_hold_time_ms(
                            static_cast<uint32_t>(v * 1000.0f));
                      }, this);
          break;
      }
      break;

    default:
      break;
  }
}

void EspressoMachineDisplay::handle_long_press_() {
  last_input_ms_ = millis();
  needs_redraw_ = true;
  beep_();

  switch (screen_) {
    case Screen::HOME:
      // Already home — optional beep, no-op.
      break;
    case Screen::EDIT_NUMBER:
      // Cancel: restore original value, return to calling menu.
      edit_ctx_.value = edit_ctx_.original;
      go_to_(edit_ctx_.return_screen);
      break;
    case Screen::CONFIRM_DIALOG:
      go_to_(confirm_ctx_.return_screen);
      break;
    default:
      // Any other screen: long press = go home.
      go_to_(Screen::HOME, 0);
      break;
  }
}

// ============================================================================
// Navigation helpers
// ============================================================================

void EspressoMachineDisplay::go_to_(Screen s, int initial_cursor) {
  screen_ = s;
  cursor_ = initial_cursor;
  needs_redraw_ = true;
}

void EspressoMachineDisplay::go_back_() {
  go_to_(Screen::HOME, 0);
}

void EspressoMachineDisplay::show_error_(const char *line1, const char *line2) {
  strncpy(error_line1_, line1, sizeof(error_line1_) - 1);
  error_line1_[sizeof(error_line1_) - 1] = '\0';
  strncpy(error_line2_, line2, sizeof(error_line2_) - 1);
  error_line2_[sizeof(error_line2_) - 1] = '\0';
  error_return_screen_ = screen_;
  error_until_ms_ = millis() + 2000;
  go_to_(Screen::ERROR_OVERLAY);
}

void EspressoMachineDisplay::begin_edit_(
    const char *label, const char *unit,
    float value, float min_val, float max_val,
    float step, float fast_step,
    Screen return_to,
    void (*apply_fn)(float, void *), void *apply_ctx) {
  strncpy(edit_ctx_.label, label, sizeof(edit_ctx_.label) - 1);
  edit_ctx_.label[sizeof(edit_ctx_.label) - 1] = '\0';
  strncpy(edit_ctx_.unit, unit, sizeof(edit_ctx_.unit) - 1);
  edit_ctx_.unit[sizeof(edit_ctx_.unit) - 1] = '\0';
  edit_ctx_.value = value;
  edit_ctx_.original = value;
  edit_ctx_.min_val = min_val;
  edit_ctx_.max_val = max_val;
  edit_ctx_.step = step;
  edit_ctx_.fast_step = fast_step;
  edit_ctx_.return_screen = return_to;
  edit_ctx_.apply_fn = apply_fn;
  edit_ctx_.apply_ctx = apply_ctx;
  edit_ctx_.rotating = false;
  edit_ctx_.rotation_start_ms = 0;
  edit_blink_on_ = true;
  edit_blink_ms_ = millis();
  go_to_(Screen::EDIT_NUMBER);
}

void EspressoMachineDisplay::begin_confirm_(
    const char *prompt, Screen return_to,
    void (*on_yes)(void *), void *on_yes_ctx) {
  strncpy(confirm_ctx_.prompt, prompt, sizeof(confirm_ctx_.prompt) - 1);
  confirm_ctx_.prompt[sizeof(confirm_ctx_.prompt) - 1] = '\0';
  confirm_ctx_.return_screen = return_to;
  confirm_ctx_.cursor_yes = true;
  confirm_ctx_.on_yes = on_yes;
  confirm_ctx_.on_yes_ctx = on_yes_ctx;
  go_to_(Screen::CONFIRM_DIALOG);
}

// ============================================================================
// Actions
// ============================================================================

void EspressoMachineDisplay::action_brew_start_() {
  if (machine_ == nullptr) return;
  if (!machine_->is_powered_on()) {
    show_error_("Cannot brew", "Machine OFF");
    return;
  }
  begin_confirm_("Start brew?", Screen::BREW_MENU,
                 [](void *ctx) {
                   auto *self = static_cast<EspressoMachineDisplay *>(ctx);
                   self->machine_->brew_start();
                   self->go_to_(Screen::HOME, 0);
                 }, this);
}

void EspressoMachineDisplay::action_brew_stop_() {
  if (machine_ != nullptr) machine_->brew_stop();
  go_to_(Screen::HOME, 0);
}

void EspressoMachineDisplay::action_steam_start_() {
  if (machine_ == nullptr) return;
  if (!machine_->is_powered_on()) {
    show_error_("Cannot steam", "Machine OFF");
    return;
  }
  begin_confirm_("Start steam?", Screen::STEAM_MENU,
                 [](void *ctx) {
                   auto *self = static_cast<EspressoMachineDisplay *>(ctx);
                   self->machine_->steam_start();
                   self->go_to_(Screen::HOME, 0);
                 }, this);
}

void EspressoMachineDisplay::action_steam_stop_() {
  if (machine_ != nullptr) machine_->steam_stop();
  go_to_(Screen::HOME, 0);
}

void EspressoMachineDisplay::action_grind_start_() {
  if (grinder_ == nullptr) return;
  begin_confirm_("Start grind?", Screen::GRINDER_MENU,
                 [](void *ctx) {
                   auto *self = static_cast<EspressoMachineDisplay *>(ctx);
                   self->grinder_->grind();
                   self->go_to_(Screen::HOME, 0);
                 }, this);
}

void EspressoMachineDisplay::action_grind_stop_() {
  if (grinder_ != nullptr) grinder_->stop();
  go_to_(Screen::HOME, 0);
}

void EspressoMachineDisplay::action_power_toggle_() {
  if (machine_ == nullptr) return;
  if (machine_->is_powered_on())
    machine_->machine_off();
  else
    machine_->machine_on();
  needs_redraw_ = true;
}

void EspressoMachineDisplay::action_flush_(float volume_ml) {
  if (machine_ != nullptr) machine_->flush(volume_ml);
  go_to_(Screen::HOME, 0);
}

// ============================================================================
// Rendering — Home Screen
// ============================================================================

void EspressoMachineDisplay::render_home_(display::DisplayBuffer &it) {
  if (machine_ == nullptr) {
    draw_title_bar_(it, "NO MACHINE");
    print_small_(it, 0, line_y_(2), "espresso_machine");
    print_small_(it, 0, line_y_(3), "not configured");
    return;
  }

  auto mode = machine_->get_mode();
  bool brewing  = (mode == espresso_machine::EspressoMode::BREWING);
  bool steaming = (mode == espresso_machine::EspressoMode::STEAMING);
  bool grinding = (grinder_ != nullptr && grinder_->is_grinding());

  if (brewing) {
    render_home_brew_active_(it);
  } else if (steaming) {
    render_home_steam_active_(it);
  } else if (grinding) {
    render_home_grind_active_(it);
  } else {
    render_home_idle_(it);
  }
}

void EspressoMachineDisplay::render_home_idle_(display::DisplayBuffer &it) {
  // ── Title bar ─────────────────────────────────────────────────────────────
  char title[22];
  float cur_temp = heater_ ? heater_->get_current_temperature() : 0.0f;
  float tgt_temp = machine_->get_brew_target_temp();
  if (!machine_->is_powered_on()) {
    snprintf(title, sizeof(title), "OFF");
  } else {
    snprintf(title, sizeof(title), "IDLE %.0f/%.0f\xb0""C", cur_temp, tgt_temp);
  }
  draw_title_bar_(it, title);

  // ── Line 1: last shot volume ──────────────────────────────────────────────
  char vol_str[22];
  if (last_shot_volume_ >= 0.0f)
    snprintf(vol_str, sizeof(vol_str), "Vol: %.1f ml", last_shot_volume_);
  else
    snprintf(vol_str, sizeof(vol_str), "Vol:  --.- ml");
  print_small_(it, 0, line_y_(1), vol_str);

  // ── Quick actions (lines 2..N) ────────────────────────────────────────────
  struct Action { const char *key; const char *label; };
  const Action items[] = {
      {"brew",     "Brew"},
      {"steam",    "Steam"},
      {"grind",    "Grind"},
      {"settings", "Settings"},
      {"power",    machine_->is_powered_on() ? "Power OFF" : "Power ON"},
  };

  int line = 2;
  int idx = 0;
  for (const auto &a : home_actions_) {
    for (const auto &item : items) {
      if (a == item.key) {
        if (a == "grind" && grinder_ == nullptr) break;
        bool selected = (idx == cursor_);
        draw_row_(it, line, selected, false, item.label);
        ++line;
        ++idx;
        break;
      }
    }
  }
  // Clamp cursor.
  if (cursor_ >= idx) cursor_ = idx - 1;
  if (cursor_ < 0) cursor_ = 0;

  // Separator before power (always last).
  draw_separator_(it, line_y_(line - 1) - 1);
}

void EspressoMachineDisplay::render_home_brew_active_(display::DisplayBuffer &it) {
  // ── Title bar ─────────────────────────────────────────────────────────────
  char title[22];
  const char *state_label = "BREWING";
  auto bst = machine_->get_brew_state();
  if (bst == espresso_machine::BrewState::HEATING)      state_label = "HEAT";
  else if (bst == espresso_machine::BrewState::COOLING) state_label = "COOLING";
  else if (bst == espresso_machine::BrewState::PRE_INFUSION) state_label = "PRE";
  else if (bst == espresso_machine::BrewState::CLEANUP) state_label = "CLEANUP";

  float cur_temp = heater_ ? heater_->get_current_temperature() : 0.0f;
  float tgt_temp = machine_->get_brew_target_temp();
  snprintf(title, sizeof(title), "%s %.0f/%.0f\xb0""C", state_label, cur_temp, tgt_temp);
  draw_title_bar_(it, title);

  // ── Volume: current / target (large font, line 1-2) ──────────────────────
  float vol = machine_->get_brew_flow_total();
  float max_vol = machine_->get_brew_flow_max();
  char vol_str[22];
  snprintf(vol_str, sizeof(vol_str), "%.1f/%.1f ml", vol, max_vol);
  print_large_(it, 0, line_y_(1), vol_str);

  // ── Time & rate (lines 3-4) ───────────────────────────────────────────────
  char time_str[16];
  format_time_(time_str, sizeof(time_str), static_cast<uint32_t>(brew_elapsed_s_()));
  char line3[22];
  snprintf(line3, sizeof(line3), "Time: %s", time_str);
  print_small_(it, 0, line_y_(3), line3);

  float rate = machine_->get_brew_flow_rate();
  char line4[22];
  snprintf(line4, sizeof(line4), "Rate: %.1f ml/s", rate);
  print_small_(it, 0, line_y_(4), line4);

  draw_separator_(it, line_y_(5));

  // ── Stop action ───────────────────────────────────────────────────────────
  draw_row_(it, 6, true, true, "Stop Brew");
}

void EspressoMachineDisplay::render_home_steam_active_(display::DisplayBuffer &it) {
  char title[22];
  const char *state_label = "STEAMING";
  auto sst = machine_->get_steam_state();
  if (sst == espresso_machine::SteamState::HEATING)  state_label = "HEAT";
  else if (sst == espresso_machine::SteamState::PURGING)  state_label = "PURGE";
  else if (sst == espresso_machine::SteamState::COOLING)  state_label = "COOL";
  else if (sst == espresso_machine::SteamState::CLEANUP)  state_label = "DONE";

  float cur_temp = heater_ ? heater_->get_current_temperature() : 0.0f;
  float tgt_temp = machine_->get_steam_target_temp();
  snprintf(title, sizeof(title), "%s %.0f/%.0f\xb0""C", state_label, cur_temp, tgt_temp);
  draw_title_bar_(it, title);

  char time_str[16];
  format_time_(time_str, sizeof(time_str), static_cast<uint32_t>(steam_elapsed_s_()));
  char line1[22];
  snprintf(line1, sizeof(line1), "Time: %s", time_str);
  print_small_(it, 0, line_y_(1), line1);

  float rate = machine_->get_brew_flow_rate();
  char line2[22];
  snprintf(line2, sizeof(line2), "Rate: %.1f ml/s", rate);
  print_small_(it, 0, line_y_(2), line2);

  uint32_t timeout_ms = machine_->get_steam_timeout_ms();
  if (timeout_ms > 0) {
    uint32_t elapsed_ms = static_cast<uint32_t>(steam_elapsed_s_() * 1000.0f);
    uint32_t remaining_s = (elapsed_ms < timeout_ms)
                               ? (timeout_ms - elapsed_ms) / 1000
                               : 0;
    char line3[22];
    format_time_(time_str, sizeof(time_str), remaining_s);
    snprintf(line3, sizeof(line3), "Timeout: %s", time_str);
    print_small_(it, 0, line_y_(3), line3);
  }

  draw_separator_(it, line_y_(5));
  draw_row_(it, 6, true, true, "Stop Steam");
}

void EspressoMachineDisplay::render_home_grind_active_(display::DisplayBuffer &it) {
  draw_title_bar_(it, "GRINDING");

  float elapsed_s = grind_elapsed_s_();
  float total_s = grinder_
                      ? static_cast<float>(grinder_->get_current_grind_time_ms()) / 1000.0f
                      : 7.0f;
  float fraction = (total_s > 0.0f) ? (elapsed_s / total_s) : 0.0f;
  if (fraction > 1.0f) fraction = 1.0f;

  // Progress bar (line 1-2)
  draw_progress_bar_(it, 2, line_y_(1), DISPLAY_W - 4, 14, fraction);

  char time_str[22];
  snprintf(time_str, sizeof(time_str), "%.1fs / %.1fs", elapsed_s, total_s);
  print_small_(it, 0, line_y_(3), time_str);

  draw_separator_(it, line_y_(5));
  draw_row_(it, 6, true, true, "Stop Grind");
}

// ============================================================================
// Rendering — Sub-menus
// ============================================================================

void EspressoMachineDisplay::render_brew_menu_(display::DisplayBuffer &it) {
  draw_title_bar_(it, "BREW");
  if (cursor_ > 5) cursor_ = 5;

  draw_row_(it, 1, cursor_ == 0, false, "Start Brew");

  char tmp[22];
  snprintf(tmp, sizeof(tmp), "Brew Temp  %.1f\xb0""C", machine_->get_brew_target_temp());
  draw_row_(it, 2, cursor_ == 1, false, tmp);

  snprintf(tmp, sizeof(tmp), "Flow Max  %.1f ml", machine_->get_brew_flow_max());
  draw_row_(it, 3, cursor_ == 2, false, tmp);

  draw_row_(it, 4, cursor_ == 3, false, "Pre-infusion  -->");

  bool surf_on = (machine_->get_temp_surf_switch() != nullptr)
                     ? machine_->get_temp_surf_switch()->state
                     : false;
  snprintf(tmp, sizeof(tmp), "Temp Surfing  %s", surf_on ? "ON" : "OFF");
  draw_row_(it, 5, cursor_ == 4, false, tmp);

  bool cooldown = machine_->get_brew_temperature_cooldown();
  snprintf(tmp, sizeof(tmp), "Cooldown  %s", cooldown ? "ON" : "OFF");
  draw_row_(it, 6, cursor_ == 5, false, tmp);

  print_small_(it, 0, line_y_(7), "[long-press=back]");
}

void EspressoMachineDisplay::render_steam_menu_(display::DisplayBuffer &it) {
  draw_title_bar_(it, "STEAM");
  if (cursor_ > 6) cursor_ = 6;

  bool steaming = (machine_->get_mode() == espresso_machine::EspressoMode::STEAMING);
  draw_row_(it, 1, cursor_ == 0, steaming, "Start Steam");
  draw_row_(it, 2, cursor_ == 1, false, "Stop Steam");

  char tmp[22];
  snprintf(tmp, sizeof(tmp), "Steam Temp %.0f\xb0""C", machine_->get_steam_target_temp());
  draw_row_(it, 3, cursor_ == 2, false, tmp);

  snprintf(tmp, sizeof(tmp), "Cool To    %.0f\xb0""C", machine_->get_steam_cool_down_to());
  draw_row_(it, 4, cursor_ == 3, false, tmp);

  snprintf(tmp, sizeof(tmp), "Purge Vol  %.1fml", machine_->get_steam_purge_volume_ml());
  draw_row_(it, 5, cursor_ == 4, false, tmp);

  snprintf(tmp, sizeof(tmp), "Flow Rate  %.1fml/s", machine_->get_steam_flow_max());
  draw_row_(it, 6, cursor_ == 5, false, tmp);

  uint32_t timeout_s = machine_->get_steam_timeout_ms() / 1000;
  char t_str[8];
  format_time_(t_str, sizeof(t_str), timeout_s);
  snprintf(tmp, sizeof(tmp), "Timeout    %s", t_str);
  draw_row_(it, 7, cursor_ == 6, false, tmp);
}

void EspressoMachineDisplay::render_grinder_menu_(display::DisplayBuffer &it) {
  draw_title_bar_(it, "GRINDER");
  if (grinder_ == nullptr) {
    print_small_(it, 0, line_y_(2), "No grinder wired");
    return;
  }
  if (cursor_ > 1) cursor_ = 1;

  draw_row_(it, 1, cursor_ == 0, false, "Grind Now");

  char tmp[22];
  float gt_s = static_cast<float>(grinder_->get_current_grind_time_ms()) / 1000.0f;
  snprintf(tmp, sizeof(tmp), "Grind Time  %.1f s", gt_s);
  draw_row_(it, 2, cursor_ == 1, false, tmp);

  print_small_(it, 0, line_y_(7), "[long-press=back]");
}

void EspressoMachineDisplay::render_settings_menu_(display::DisplayBuffer &it) {
  draw_title_bar_(it, "SETTINGS");
  if (cursor_ > 6) cursor_ = 6;

  char tmp[22];
  snprintf(tmp, sizeof(tmp), "Brew Temp  %.1f\xb0""C", machine_->get_brew_target_temp());
  draw_row_(it, 1, cursor_ == 0, false, tmp);

  snprintf(tmp, sizeof(tmp), "Steam Temp %.0f\xb0""C", machine_->get_steam_target_temp());
  draw_row_(it, 2, cursor_ == 1, false, tmp);

  snprintf(tmp, sizeof(tmp), "Flow Max   %.1f ml", machine_->get_brew_flow_max());
  draw_row_(it, 3, cursor_ == 2, false, tmp);

  snprintf(tmp, sizeof(tmp), "Flow Off   %.1f ml", machine_->get_brew_flow_offset());
  draw_row_(it, 4, cursor_ == 3, false, tmp);

  if (grinder_ != nullptr) {
    float gt_s = static_cast<float>(grinder_->get_current_grind_time_ms()) / 1000.0f;
    snprintf(tmp, sizeof(tmp), "Grind Time %.1f s", gt_s);
  } else {
    snprintf(tmp, sizeof(tmp), "Grind Time  --");
  }
  draw_row_(it, 5, cursor_ == 4, false, tmp);

  draw_row_(it, 6, cursor_ == 5, false, "Pre-infusion  -->");
  draw_row_(it, 7, cursor_ == 6, false, "v more...");
}

void EspressoMachineDisplay::render_settings_menu_p2_(display::DisplayBuffer &it) {
  draw_title_bar_(it, "SETTINGS (2/2)");
  if (cursor_ > 3) cursor_ = 3;

  bool surf_on = (machine_->get_temp_surf_switch() != nullptr)
                     ? machine_->get_temp_surf_switch()->state
                     : false;
  char tmp[22];
  snprintf(tmp, sizeof(tmp), "Temp Surfing  %s", surf_on ? "ON" : "OFF");
  draw_row_(it, 1, cursor_ == 0, false, tmp);

  bool cooldown = machine_->get_brew_temperature_cooldown();
  snprintf(tmp, sizeof(tmp), "Temp Cooldown %s", cooldown ? "ON" : "OFF");
  draw_row_(it, 2, cursor_ == 1, false, tmp);

  float idle_min = static_cast<float>(machine_->get_idle_timeout_ms()) / 60000.0f;
  snprintf(tmp, sizeof(tmp), "Idle Timeout  %.0fmin", idle_min);
  draw_row_(it, 3, cursor_ == 2, false, tmp);

  draw_row_(it, 4, cursor_ == 3, false, "Maintenance  -->");
}

void EspressoMachineDisplay::render_maintenance_menu_(display::DisplayBuffer &it) {
  draw_title_bar_(it, "MAINTENANCE");
  if (cursor_ > 2) cursor_ = 2;

  char tmp[22];
  snprintf(tmp, sizeof(tmp), "Flush %.0f ml", flush_vol_ml_);
  draw_row_(it, 1, cursor_ == 0, false, tmp);

  snprintf(tmp, sizeof(tmp), "Flush Vol  %.0f ml", flush_vol_ml_);
  draw_row_(it, 2, cursor_ == 1, false, tmp);

  draw_row_(it, 3, cursor_ == 2, false, "Restart ESP");

  print_small_(it, 0, line_y_(7), "[long-press=back]");
}

void EspressoMachineDisplay::render_pre_infusion_menu_(display::DisplayBuffer &it) {
  draw_title_bar_(it, "PRE-INFUSION");
  if (cursor_ > 2) cursor_ = 2;

  char tmp[22];
  snprintf(tmp, sizeof(tmp), "Enabled  %s", pi_enabled_ ? "ON" : "OFF");
  draw_row_(it, 1, cursor_ == 0, false, tmp);

  snprintf(tmp, sizeof(tmp), "Volume  %.1f ml", pi_volume_ml_);
  draw_row_(it, 2, cursor_ == 1, false, tmp);

  snprintf(tmp, sizeof(tmp), "Hold Time  %.1f s", pi_hold_s_);
  draw_row_(it, 3, cursor_ == 2, false, tmp);

  print_small_(it, 0, line_y_(7), "[long-press=back]");
}

void EspressoMachineDisplay::render_edit_number_(display::DisplayBuffer &it) {
  draw_title_bar_(it, edit_ctx_.label);

  // Centre the value + unit in the display.
  char val_str[22];
  // Show value with 1 decimal for most, fewer for large ranges.
  snprintf(val_str, sizeof(val_str), "[ %.1f ] %s", edit_ctx_.value, edit_ctx_.unit);

  int y = line_y_(3);
  // Blink the brackets by toggling '[]' with spaces.
  if (!edit_blink_on_) {
    snprintf(val_str, sizeof(val_str), "  %.1f   %s", edit_ctx_.value, edit_ctx_.unit);
  }
  print_large_(it, 4, y, val_str);

  print_small_(it, 0, line_y_(5), "< rotate to change");
  print_small_(it, 0, line_y_(6), "* click to confirm");
  print_small_(it, 0, line_y_(7), "X long-press=cancel");
}

void EspressoMachineDisplay::render_confirm_dialog_(display::DisplayBuffer &it) {
  // Empty title bar.
  draw_title_bar_(it, "");

  print_small_(it, 4, line_y_(2), confirm_ctx_.prompt);

  // YES / NO options.
  char yes_str[12];
  char no_str[12];
  snprintf(yes_str, sizeof(yes_str), "%s YES", confirm_ctx_.cursor_yes ? ">" : " ");
  snprintf(no_str, sizeof(no_str), "%s NO", confirm_ctx_.cursor_yes ? "  " : ">");
  print_small_(it, 8, line_y_(4), yes_str);
  print_small_(it, 70, line_y_(4), no_str);

  print_small_(it, 0, line_y_(7), "[long-press=cancel]");
}

void EspressoMachineDisplay::render_error_overlay_(display::DisplayBuffer &it) {
  draw_title_bar_(it, "");
  print_small_(it, 4, line_y_(2), "! Cannot proceed");
  print_small_(it, 4, line_y_(3), error_line1_);
  print_small_(it, 4, line_y_(4), error_line2_);
  print_small_(it, 4, line_y_(6), "(returns in 2 s)");
}

// ============================================================================
// Drawing primitives
// ============================================================================

void EspressoMachineDisplay::print_small_(display::DisplayBuffer &it,
                                          int x, int y, const char *text) {
  if (font_small_ != nullptr) {
    it.print(x, y, font_small_, text);
  }
  // No-op when no font is configured — display shows UI chrome only.
}

void EspressoMachineDisplay::print_large_(display::DisplayBuffer &it,
                                          int x, int y, const char *text) {
  if (font_large_ != nullptr) {
    it.print(x, y, font_large_, text);
  } else if (font_small_ != nullptr) {
    it.print(x, y, font_small_, text);
  }
  // No-op when no font is configured.
}

void EspressoMachineDisplay::draw_title_bar_(display::DisplayBuffer &it,
                                             const char *title) {
  // Fill title bar with lit pixels (inverted background).
  it.filled_rectangle(0, 0, DISPLAY_W, FONT_H);
  // Draw text in dark pixels on the lit background (only when font available).
  if (font_small_ != nullptr) {
    it.print(1, 0, font_small_, CLR_OFF,
             display::TextAlign::TOP_LEFT, title);
  }
}

void EspressoMachineDisplay::draw_separator_(display::DisplayBuffer &it, int y) {
  if (theme_ == Theme::MINIMAL) return;
  it.horizontal_line(0, y, DISPLAY_W);
}

int EspressoMachineDisplay::draw_row_(display::DisplayBuffer &it, int line,
                                      bool is_selected, bool is_active,
                                      const char *label) {
  int y = line_y_(line);
  int x = 0;

  // Cursor glyph.
  if (is_selected) {
    print_small_(it, x, y, cursor_glyph_());
  }
  x += 6;  // cursor column width

  // Active marker.
  if (is_active) {
    print_small_(it, x, y, active_marker_());
  }
  x += 6;  // marker column width

  // Label text.
  print_small_(it, x, y, label);
  return x + static_cast<int>(strlen(label)) * 6;
}

void EspressoMachineDisplay::draw_progress_bar_(display::DisplayBuffer &it,
                                                 int x, int y, int w, int h,
                                                 float fraction) {
  // Outline.
  it.rectangle(x, y, w, h);
  // Fill.
  int fill_w = static_cast<int>((w - 2) * fraction);
  if (fill_w > 0) {
    it.filled_rectangle(x + 1, y + 1, fill_w, h - 2);
  }
}

// ============================================================================
// Theme helpers
// ============================================================================

const char *EspressoMachineDisplay::cursor_glyph_() const {
  switch (theme_) {
    case Theme::MINIMAL: return ">";
    case Theme::BARISTA: return "*";
    default:             return ">";  // '▶' is multi-byte; use ASCII for safety
  }
}

const char *EspressoMachineDisplay::active_marker_() const {
  switch (theme_) {
    case Theme::MINIMAL: return "*";
    default:             return "*";
  }
}

// ============================================================================
// Elapsed time helpers
// ============================================================================

float EspressoMachineDisplay::brew_elapsed_s_() const {
  if (machine_ == nullptr ||
      machine_->get_mode() != espresso_machine::EspressoMode::BREWING)
    return 0.0f;
  return static_cast<float>(millis() - brew_start_ms_) / 1000.0f;
}

float EspressoMachineDisplay::steam_elapsed_s_() const {
  if (machine_ == nullptr ||
      machine_->get_mode() != espresso_machine::EspressoMode::STEAMING)
    return 0.0f;
  return static_cast<float>(millis() - steam_start_ms_) / 1000.0f;
}

float EspressoMachineDisplay::grind_elapsed_s_() const {
  if (grinder_ == nullptr || !grinder_->is_grinding()) return 0.0f;
  return static_cast<float>(millis() - grind_start_ms_) / 1000.0f;
}

void EspressoMachineDisplay::format_time_(char *buf, size_t len, uint32_t seconds) {
  snprintf(buf, len, "%u:%02u", seconds / 60, seconds % 60);
}

// ============================================================================
// Miscellaneous
// ============================================================================

bool EspressoMachineDisplay::has_home_action_(const char *action) const {
  for (const auto &a : home_actions_) {
    if (a == action) return true;
  }
  return false;
}

void EspressoMachineDisplay::wake_screensaver_() {
  last_input_ms_ = millis();
  if (screensaver_active_) {
    screensaver_active_ = false;
    needs_redraw_ = true;
  }
}

void EspressoMachineDisplay::beep_() {
  if (beeper_pin_ == nullptr) return;
  beeper_pin_->digital_write(true);
  // Brief beep; turn off in next loop tick handled by a timer.
  // Simple implementation: just toggle (no PWM tone in this phase).
  // Full PWM beep deferred to Phase 10e.
  beeper_pin_->digital_write(false);
}

}  // namespace espresso_machine_display
}  // namespace esphome
