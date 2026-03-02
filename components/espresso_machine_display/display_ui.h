#pragma once

#include <cstring>
#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/display/display_buffer.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"

#include "../espresso_machine/espresso_machine.h"
#include "../espresso_machine_grinder/grinder.h"
#include "../espresso_machine/interfaces.h"

namespace esphome {
namespace espresso_machine_display {

// ---------------------------------------------------------------------------
// Theme — visual style applied to all screens
// ---------------------------------------------------------------------------
enum class Theme : uint8_t {
  CLASSIC = 0,  // inverted title, ▶ cursor, separator lines
  MINIMAL = 1,  // plain monospace, no separators
  BARISTA = 2,  // large hero temperature, compact action strip
  DARK = 3,     // full white-on-black
};

// ---------------------------------------------------------------------------
// Screen — navigation state machine
// ---------------------------------------------------------------------------
enum class Screen : uint8_t {
  HOME = 0,
  BREW_MENU,
  STEAM_MENU,
  GRINDER_MENU,
  SETTINGS_MENU,
  SETTINGS_MENU_P2,
  MAINTENANCE_MENU,
  PRE_INFUSION_MENU,
  EDIT_NUMBER,
  CONFIRM_DIALOG,
  ERROR_OVERLAY,
};

// ---------------------------------------------------------------------------
// Number-edit context (one instance, reused for every editable field)
// ---------------------------------------------------------------------------
struct EditContext {
  char label[22];     // title bar text
  char unit[8];       // unit suffix string
  float value;        // live-edited value
  float original;     // saved on entry; restored on cancel
  float min_val;
  float max_val;
  float step;         // normal step size
  float fast_step;    // step after > 1 s of continuous rotation
  Screen return_screen;   // screen to go back to on confirm / cancel
  void (*apply_fn)(float value, void *ctx);
  void *apply_ctx;
  // Acceleration tracking
  uint32_t rotation_start_ms;   // when continuous rotation began
  bool rotating;
};

// ---------------------------------------------------------------------------
// Confirm dialog context (one instance, reused for every confirmation)
// ---------------------------------------------------------------------------
struct ConfirmContext {
  char prompt[22];    // main question line
  Screen return_screen;
  bool cursor_yes;    // true = YES highlighted
  void (*on_yes)(void *ctx);
  void *on_yes_ctx;
};

// ---------------------------------------------------------------------------
// EspressoMachineDisplay — ST7920 128×64 graphical LCD menu controller
//
// Driven by a rotary encoder + push-button, this component renders a
// full-featured action-hub UI on top of the espresso_machine orchestrator.
// All hardware actions go through the orchestrator's public API, identical
// to how Home Assistant automations call them — no safety logic is bypassed.
// ---------------------------------------------------------------------------
class EspressoMachineDisplay : public Component {
 public:
  // ----- Hardware bindings (set by Python codegen) -------------------------
  void set_display(display::DisplayBuffer *d) { display_ = d; }
  void set_encoder(sensor::Sensor *s);
  void set_button(binary_sensor::BinarySensor *b);

  // ----- Component bindings ------------------------------------------------
  void set_espresso_machine(espresso_machine::EspressoMachine *m) { machine_ = m; }
  void set_grinder(espresso_machine_grinder::Grinder *g) { grinder_ = g; }
  // Interface-typed setters — accept IHeater* / IFlowMeter* directly so the
  // display component does not depend on concrete heater/flow_meter headers.
  // The Python codegen passes concrete types which are implicitly upcast.
  void set_heater(espresso_machine::IHeater *h) { heater_ = h; }
  void set_flow_meter(espresso_machine::IFlowMeter *f) { flow_meter_ = f; }

  // ----- Fonts (optional) --------------------------------------------------
  void set_font_small(display::BaseFont *f) { font_small_ = f; }
  void set_font_large(display::BaseFont *f) { font_large_ = f; }

  // ----- Theme / behaviour -------------------------------------------------
  void set_theme(Theme t) { theme_ = t; }
  void set_long_press_ms(uint32_t ms) { long_press_ms_ = ms; }
  void set_screensaver_timeout_ms(uint32_t ms) { screensaver_timeout_ms_ = ms; }
  void set_beeper_pin(GPIOPin *pin) { beeper_pin_ = pin; }
  void add_home_action(const std::string &action) { home_actions_.push_back(action); }

  // ----- ESPHome lifecycle -------------------------------------------------
  void setup() override;
  void loop() override;

  // ----- Public render entry point (called from display lambda) ------------
  void render(display::DisplayBuffer &it);

 protected:
  // ── Hardware ──────────────────────────────────────────────────────────────
  display::DisplayBuffer *display_{nullptr};
  sensor::Sensor *encoder_{nullptr};
  binary_sensor::BinarySensor *button_{nullptr};
  GPIOPin *beeper_pin_{nullptr};

  // ── Components ────────────────────────────────────────────────────────────
  espresso_machine::EspressoMachine *machine_{nullptr};
  espresso_machine_grinder::Grinder *grinder_{nullptr};

  // Typed pointers — set directly by set_heater/set_flow_meter using interface types.
  // IHeater and IFlowMeter are defined in interfaces.h; the display component
  // is decoupled from the concrete heater/flow_meter implementations.
  espresso_machine::IHeater *heater_{nullptr};
  espresso_machine::IFlowMeter *flow_meter_{nullptr};

  // ── Fonts ─────────────────────────────────────────────────────────────────
  display::BaseFont *font_small_{nullptr};
  display::BaseFont *font_large_{nullptr};

  // ── Config ────────────────────────────────────────────────────────────────
  Theme theme_{Theme::CLASSIC};
  uint32_t long_press_ms_{1000};
  uint32_t screensaver_timeout_ms_{300000};  // 5 min
  std::vector<std::string> home_actions_;

  // ── Navigation state ──────────────────────────────────────────────────────
  Screen screen_{Screen::HOME};
  int cursor_{0};
  bool needs_redraw_{true};
  uint32_t last_input_ms_{0};
  bool screensaver_active_{false};
  uint32_t last_live_refresh_ms_{0};  // periodic refresh on active-mode screens

  // ── Encoder state ─────────────────────────────────────────────────────────
  float last_encoder_val_{0.0f};
  bool encoder_subscribed_{false};

  // ── Button state (click / long-press detection) ───────────────────────────
  bool button_was_pressed_{false};
  uint32_t button_press_ms_{0};
  bool long_press_fired_{false};
  bool click_pending_{false};

  // ── Active-mode elapsed time tracking ─────────────────────────────────────
  uint32_t brew_start_ms_{0};
  uint32_t steam_start_ms_{0};
  uint32_t grind_start_ms_{0};
  bool was_brewing_{false};
  bool was_steaming_{false};
  bool was_grinding_{false};

  // ── Last completed shot volume (shown on Home idle) ───────────────────────
  float last_shot_volume_{-1.0f};  // < 0 = none yet

  // ── Error overlay ─────────────────────────────────────────────────────────
  char error_line1_[22]{};
  char error_line2_[22]{};
  uint32_t error_until_ms_{0};
  Screen error_return_screen_{Screen::HOME};

  // ── Number edit state ─────────────────────────────────────────────────────
  EditContext edit_ctx_{};
  bool edit_blink_on_{true};
  uint32_t edit_blink_ms_{0};

  // ── Confirm dialog state ──────────────────────────────────────────────────
  ConfirmContext confirm_ctx_{};

  // ── Pre-infusion values (display mirrors machine values) ──────────────────
  bool pi_enabled_{false};
  float pi_volume_ml_{5.0f};
  float pi_hold_s_{5.0f};

  // ── Settings page 2 flush volume ─────────────────────────────────────────
  float flush_vol_ml_{50.0f};

  // =========================================================================
  // Input handling
  // =========================================================================
  void handle_input_();
  void handle_encoder_delta_(int delta);
  void handle_click_();
  void handle_long_press_();

  // =========================================================================
  // Screen rendering — one function per screen
  // =========================================================================
  void render_home_(display::DisplayBuffer &it);
  void render_home_idle_(display::DisplayBuffer &it);
  void render_home_brew_active_(display::DisplayBuffer &it);
  void render_home_steam_active_(display::DisplayBuffer &it);
  void render_home_grind_active_(display::DisplayBuffer &it);
  void render_brew_menu_(display::DisplayBuffer &it);
  void render_steam_menu_(display::DisplayBuffer &it);
  void render_grinder_menu_(display::DisplayBuffer &it);
  void render_settings_menu_(display::DisplayBuffer &it);
  void render_settings_menu_p2_(display::DisplayBuffer &it);
  void render_maintenance_menu_(display::DisplayBuffer &it);
  void render_pre_infusion_menu_(display::DisplayBuffer &it);
  void render_edit_number_(display::DisplayBuffer &it);
  void render_confirm_dialog_(display::DisplayBuffer &it);
  void render_error_overlay_(display::DisplayBuffer &it);

  // =========================================================================
  // Navigation helpers
  // =========================================================================
  void go_to_(Screen s, int initial_cursor = 0);
  void go_back_();

  void show_error_(const char *line1, const char *line2 = "");

  // Number-edit entry point: sets up edit_ctx_ and transitions to EDIT_NUMBER.
  void begin_edit_(const char *label, const char *unit,
                   float value, float min_val, float max_val,
                   float step, float fast_step,
                   Screen return_to,
                   void (*apply_fn)(float, void *), void *apply_ctx);

  // Confirmation dialog entry point.
  void begin_confirm_(const char *prompt,
                      Screen return_to,
                      void (*on_yes)(void *), void *on_yes_ctx);

  // =========================================================================
  // Actions (invoked from menus; all go through machine_ public API)
  // =========================================================================
  void action_brew_start_();
  void action_brew_stop_();
  void action_steam_start_();
  void action_steam_stop_();
  void action_grind_start_();
  void action_grind_stop_();
  void action_power_toggle_();
  void action_flush_(float volume_ml);

  // =========================================================================
  // Drawing primitives
  // =========================================================================
  // Print using font_small_ (falls back to print() without font if null)
  void print_small_(display::DisplayBuffer &it, int x, int y, const char *text);
  // Print using font_large_ (falls back to font_small_ if null)
  void print_large_(display::DisplayBuffer &it, int x, int y, const char *text);

  // Inverted title bar (line 0): filled rectangle + small-font text.
  void draw_title_bar_(display::DisplayBuffer &it, const char *title);

  // Horizontal separator line at pixel row y.
  void draw_separator_(display::DisplayBuffer &it, int y);

  // A row with optional cursor glyph, optional bullet marker, and label.
  // Returns the x-offset after the glyphs (for appending value text).
  int draw_row_(display::DisplayBuffer &it, int line, bool is_selected,
                bool is_active, const char *label);

  // Progress bar drawn at (x, y) with width w and height h, filled fraction [0,1].
  void draw_progress_bar_(display::DisplayBuffer &it, int x, int y, int w, int h,
                           float fraction);

  // =========================================================================
  // Theme helpers
  // =========================================================================
  const char *cursor_glyph_() const;
  const char *active_marker_() const;

  // =========================================================================
  // Miscellaneous
  // =========================================================================
  // Compute brew/steam/grind elapsed seconds based on machine state.
  float brew_elapsed_s_() const;
  float steam_elapsed_s_() const;
  float grind_elapsed_s_() const;

  // Format elapsed time (integer seconds) as "M:SS" into buf.
  static void format_time_(char *buf, size_t len, uint32_t seconds);

  // Check if a home action is configured.
  bool has_home_action_(const char *action) const;

  // Wake screensaver on any input.
  void wake_screensaver_();

  // Beep (if beeper_pin_ is configured).
  void beep_();

  // Pixel row for a menu line (line 0 = title bar).
  static constexpr int line_y_(int line) { return line * 8; }

  // Number of home-screen action rows (lines 2..N).
  static constexpr int DISPLAY_W = 128;
  static constexpr int DISPLAY_H = 64;
  static constexpr int FONT_H = 8;     // small font pixel height
  static constexpr int FONT_LARGE_H = 16;
};

}  // namespace espresso_machine_display
}  // namespace esphome
