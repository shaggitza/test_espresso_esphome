#pragma once

#include <cmath>
#include <functional>
#include <string>
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/number/number.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/automation.h"
#include "interfaces.h"

namespace esphome {
namespace espresso_machine {

// ---------------------------------------------------------------------------
// Brew state machine
// ---------------------------------------------------------------------------
enum class BrewState : uint8_t {
  IDLE = 0,
  HEATING = 1,       // waiting for thermoblock to reach brew temperature
  PRE_INFUSION = 2,  // low-pressure pre-wet of the puck
  BREWING = 3,       // full-pressure extraction
  DONE = 4,          // flow_max reached; waiting for cleanup
  CLEANUP = 5,       // flushing brew path
  COOLING = 6,       // temperature cooldown: purge valve open, pump running, waiting for temp drop
};

// ---------------------------------------------------------------------------
// Steam state machine
// ---------------------------------------------------------------------------
enum class SteamState : uint8_t {
  IDLE = 0,
  HEATING = 1,   // waiting for thermoblock to reach steam temperature
  PURGING = 2,   // pump running through purge valve to clear water before steam
  STEAMING = 3,  // steam valve open; pump pulsing
  COOLING = 4,   // heater setpoint lowered; waiting for temp to drop
  CLEANUP = 5,   // purging steam path
};

// ---------------------------------------------------------------------------
// Top-level mode (used for grinder lockout, display, HA sensor)
// ---------------------------------------------------------------------------
enum class EspressoMode : uint8_t {
  IDLE = 0,
  BREWING = 1,
  STEAMING = 2,
  FLUSHING = 3,  // P2-2: maintenance flush (pump N ml through purge valve)
};

// ---------------------------------------------------------------------------
// Forward declaration for BrewFlowMaxNumber
// ---------------------------------------------------------------------------
class EspressoMachine;

// ---------------------------------------------------------------------------
// BrewFlowMaxNumber — number entity exposing the brew flow max to HA
// ---------------------------------------------------------------------------
class BrewFlowMaxNumber : public number::Number {
 public:
  BrewFlowMaxNumber() = default;
  void set_parent(EspressoMachine *parent) { parent_ = parent; }

 protected:
  void control(float value) override;

  EspressoMachine *parent_{nullptr};
};

// ---------------------------------------------------------------------------
// TempSurfSwitch — switch entity enabling/disabling temperature surfing from HA
// ---------------------------------------------------------------------------
class TempSurfSwitch : public switch_::Switch {
 public:
  TempSurfSwitch() = default;
  void set_parent(EspressoMachine *parent) { parent_ = parent; }

 protected:
  void write_state(bool state) override;

  EspressoMachine *parent_{nullptr};
};

// ---------------------------------------------------------------------------
// EspressoMachine — pure orchestrator, owns no hardware
// ---------------------------------------------------------------------------
class EspressoMachine : public Component {
 public:
  // ----- Brew hardware setters (called by Python codegen) -----------------
  // Heater is a native ESPHome climate entity; stored as Component* here.
  void set_brew_heater(Component *h) { brew_heater_ = h; }
  void set_brew_valve(IValve *v) { brew_valve_ = v; }
  void set_brew_purge_valve(IValve *v) { brew_purge_valve_ = v; }
  void set_brew_pump(IPump *p) { brew_pump_ = p; }

  // ----- Brew temperature controller (IHeater) ----------------------------
  // Optional: when set the brew state machine calls set_target_temperature()
  // at shot start and gates the HEATING→BREWING transition on actual temperature.
  // Also used to apply the temperature-surfing ramp during extraction.
  // If not wired the machine transitions from HEATING immediately (backward compat).
  void set_brew_heater_ctrl(IHeater *h) { brew_heater_ctrl_ = h; }

  // ----- Brew configuration setters ----------------------------------------
  void set_brew_target_temperature(float t) { brew_target_temp_ = t; }
  void set_brew_flow_max(float ml) { brew_flow_max_ml_ = ml; }
  void set_brew_flow_offset(float ml) { brew_flow_offset_ml_ = ml; }
  void set_brew_flow_max_number(BrewFlowMaxNumber *n) { brew_flow_max_number_ = n; }
  // When true the brew sequence inserts a COOLING state after DONE: the purge
  // valve is opened and the pump runs in bypass mode until the thermoblock
  // cools back to brew_target_temp_.  Requires brew_heater_ctrl_ to be wired;
  // if not wired the option is silently ignored.
  void set_brew_temperature_cooldown(bool enabled) { brew_temperature_cooldown_ = enabled; }

  // ----- Temperature surfing setters (Phase 7) -----------------------------
  void set_brew_temp_offset(float offset) { brew_temp_offset_ = offset; }
  void set_brew_temp_ramp_time_ms(uint32_t ms) { brew_temp_ramp_time_ms_ = ms; }
  // Runtime enable/disable switch wired from HA (optional — if not set,
  // temp surfing is governed purely by offset/ramp_time being non-zero).
  void set_temp_surf_enabled(bool enabled) { temp_surf_enabled_ = enabled; }
  void set_temp_surf_switch(TempSurfSwitch *sw) { temp_surf_switch_ = sw; }

  // ----- Pre-infusion setters (Phase 7) ------------------------------------
  void set_pre_infusion_enabled(bool enabled) { pre_infusion_enabled_ = enabled; }
  void set_pre_infusion_volume_ml(float ml) { pre_infusion_volume_ml_ = ml; }
  void set_pre_infusion_hold_time_ms(uint32_t ms) { pre_infusion_hold_time_ms_ = ms; }

  // ----- Steam hardware setters --------------------------------------------
  // Heater shared with brew; stored as Component* until Phase 2 climate wiring.
  void set_steam_heater(Component *h) { steam_heater_ = h; }
  void set_steam_valve(IValve *v) { steam_valve_ = v; }
  void set_steam_purge_valve(IValve *v) { steam_purge_valve_ = v; }
  void set_steam_pump(IPump *p) { steam_pump_ = p; }

  // ----- Steam temperature controller (IHeater) ----------------------------
  // Optional: when set the state machine gates HEATING and COOLING transitions
  // on actual temperature.  If not wired the machine transitions immediately
  // (same as the Phase 1 placeholder behaviour).
  void set_steam_heater_ctrl(IHeater *h) { steam_heater_ctrl_ = h; }

  // ----- Steam configuration setters ---------------------------------------
  void set_steam_target_temperature(float t) { steam_target_temp_ = t; }
  void set_steam_flow_max(float ml_per_s) { steam_flow_max_ml_per_s_ = ml_per_s; }
  void set_steam_cool_down_to(float t) { steam_cool_down_to_ = t; }
  // Volume (ml) to pump through the purge valve before opening the steam valve.
  // 0 = skip purge phase (default, backward compatible).
  void set_steam_purge_volume_ml(float ml) { steam_purge_volume_ml_ = ml; }
  // Maximum steaming duration (ms). 0 = disabled (default).
  void set_steam_timeout_ms(uint32_t ms) { steam_timeout_ms_ = ms; }

  // ----- Safety: hard over-temperature cutoff (P0-1) -----------------------
  // When a temperature sensor exceeds the cutoff limit the orchestrator
  // immediately stops all activity and latches a safety flag.  Cleared only
  // by a device reboot (i.e. it is a true hard interlock, not auto-reset).
  void set_over_temp_sensor(IHeater *h) { over_temp_sensor_ = h; }
  void set_over_temp_cutoff_limit(float limit) { over_temp_limit_ = limit; }
  bool is_over_temp_cutoff_triggered() const { return over_temp_cutoff_triggered_; }

  // ----- Safety: brew timeout (P0-4) ---------------------------------------
  // If the brew sequence has not completed within this many milliseconds the
  // orchestrator stops the shot.  0 = disabled (default).  Covers the case
  // where a Wi-Fi / HA disconnect prevents a manual stop and the flow sensor
  // returns 0 so flow_max is never reached.
  void set_brew_timeout_ms(uint32_t ms) { brew_timeout_ms_ = ms; }

  // ----- ESPHome lifecycle --------------------------------------------------
  void setup() override;
  void loop() override;

  // ----- Power control (on/off toggle) -------------------------------------
  // machine_on():  powers the machine on; brew_start / steam_start become active.
  // machine_off(): safely shuts down.  If steaming, waits for the cool-down +
  //               purge sequence to complete before returning to IDLE.
  void machine_on();
  void machine_off();
  bool is_powered_on() const { return powered_on_; }

  // ----- Public actions (callable from YAML / HA automations) --------------
  void brew_start();
  void brew_stop();
  void steam_start();
  void steam_stop();
  // Flush: pump `volume_ml` ml through the brew purge valve (P2-2).
  // Useful for group-head rinsing between shots. Only accepted when IDLE.
  void flush(float volume_ml);

  // ----- Status accessors ---------------------------------------------------
  EspressoMode get_mode() const { return mode_; }
  const char *mode_name() const;
  // Returns a verbose, human-readable status string combining mode, sub-state,
  // and current sensor values (temperature, flow).  Published to the optional
  // status_sensor text entity immediately on every state change and on every
  // loop() tick so the user sees live progress without polling from HA.
  // Examples: "Heating to 90.0°C — currently 85.3°C",
  //           "Brewing: 15.2 ml / 40.0 ml",
  //           "Cooling to 90.0°C — currently 125.3°C"
  std::string status_name() const;
  BrewState get_brew_state() const { return brew_state_; }
  SteamState get_steam_state() const { return steam_state_; }

  // ----- Shot stats (available after each completed shot) ------------------
  float get_last_shot_time_s() const { return last_shot_time_s_; }
  float get_last_shot_volume_ml() const { return last_shot_volume_ml_; }
  float get_last_shot_yield_ml() const {
    return last_shot_volume_ml_ > 0.0f ? last_shot_volume_ml_ - brew_flow_offset_ml_ : 0.0f;
  }

  // ----- Shot stat HA sensor entities (P1-5) --------------------------------
  void set_last_shot_time_sensor(sensor::Sensor *s) { last_shot_time_sensor_ = s; }
  void set_last_shot_volume_sensor(sensor::Sensor *s) { last_shot_volume_sensor_ = s; }
  void set_last_shot_yield_sensor(sensor::Sensor *s) { last_shot_yield_sensor_ = s; }

  // ----- Status text sensor — updated on every state transition (optional) --
  // When wired the orchestrator publishes its current detailed status string
  // (e.g. "Brew: Heating", "Brewing", "Steam: Cooling") to Home Assistant
  // immediately on every state change, giving real-time feedback to the user.
  void set_status_sensor(text_sensor::TextSensor *s) { status_sensor_ = s; }

  // ----- Cleanup action callbacks (P2-1) ------------------------------------
  // Called in the brew/steam DONE→CLEANUP transition.  Set from Python codegen
  // using a lambda or script reference via `cleanup_script:` in YAML.
  void set_brew_cleanup_fn(std::function<void()> fn) { brew_cleanup_fn_ = std::move(fn); }
  void set_steam_cleanup_fn(std::function<void()> fn) { steam_cleanup_fn_ = std::move(fn); }

  // ----- Safety query for grinder lockout -----------------------------------
  bool is_busy() const { return mode_ != EspressoMode::IDLE; }

 protected:
  // -- Mode / state ---------------------------------------------------------
  EspressoMode mode_{EspressoMode::IDLE};
  BrewState brew_state_{BrewState::IDLE};
  SteamState steam_state_{SteamState::IDLE};

  // -- Power state -----------------------------------------------------------
  // Defaults to false (off) on boot for safety. Call machine_on() to enable.
  bool powered_on_{false};

  // -- Brew hardware ---------------------------------------------------------
  Component *brew_heater_{nullptr};  // native ESPHome climate entity reference
  IHeater *brew_heater_ctrl_{nullptr};  // optional temperature controller (P1-2)
  IValve *brew_valve_{nullptr};
  IValve *brew_purge_valve_{nullptr};
  IPump *brew_pump_{nullptr};

  // -- Brew config -----------------------------------------------------------
  float brew_target_temp_{90.0f};     // °C
  float brew_flow_max_ml_{40.0f};     // ml to extract before stopping
  float brew_flow_offset_ml_{20.0f};  // ml absorbed by puck (subtracted for yield)
  bool brew_temperature_cooldown_{false};  // when true, cool to brew_target_temp_ after shot
  BrewFlowMaxNumber *brew_flow_max_number_{nullptr};

  // -- Temperature surfing config (Phase 7) ----------------------------------
  float brew_temp_offset_{0.0f};        // °C added to setpoint at shot start
  uint32_t brew_temp_ramp_time_ms_{0};  // ms to ramp back to target temperature
  bool temp_surf_enabled_{true};        // runtime toggle (controlled from HA switch)
  TempSurfSwitch *temp_surf_switch_{nullptr};

  // -- Pre-infusion config (Phase 7) -----------------------------------------
  bool pre_infusion_enabled_{false};
  float pre_infusion_volume_ml_{5.0f};         // ml to push during pre-infusion
  uint32_t pre_infusion_hold_time_ms_{5000};   // ms to hold after pre-infusion flowing

  // -- Steam hardware --------------------------------------------------------
  Component *steam_heater_{nullptr};  // shared native climate entity reference
  IValve *steam_valve_{nullptr};
  IValve *steam_purge_valve_{nullptr};
  IPump *steam_pump_{nullptr};
  IHeater *steam_heater_ctrl_{nullptr};  // optional temperature controller

  // -- Steam config ----------------------------------------------------------
  float steam_target_temp_{135.0f};       // °C
  float steam_flow_max_ml_per_s_{2.0f};   // ml/s target flow rate while steaming
  float steam_cool_down_to_{90.0f};       // °C — heater setpoint after steaming
  float steam_purge_volume_ml_{0.0f};     // ml to purge before steaming (0 = skip)
  uint32_t steam_timeout_ms_{0};          // max steaming duration ms (0 = disabled)

  // -- Internal state --------------------------------------------------------
  uint32_t state_entered_ms_{0};  // millis() when current brew/steam state was entered
  uint32_t steam_start_ms_{0};    // millis() when STEAMING state was entered

  // -- Brewing tracking (Phase 7) -------------------------------------------
  bool pre_infusion_flowing_{true};       // true=flowing phase, false=hold phase
  uint32_t pre_infusion_hold_start_ms_{0};
  uint32_t brew_shot_start_ms_{0};        // millis() when BREWING state was entered

  // -- Shot stats (Phase 7) --------------------------------------------------
  float last_shot_time_s_{0.0f};
  float last_shot_volume_ml_{0.0f};

  // -- Shot stat HA sensor entities (P1-5) -----------------------------------
  sensor::Sensor *last_shot_time_sensor_{nullptr};
  sensor::Sensor *last_shot_volume_sensor_{nullptr};
  sensor::Sensor *last_shot_yield_sensor_{nullptr};

  // -- Status text sensor ----------------------------------------------------
  text_sensor::TextSensor *status_sensor_{nullptr};
  // Last string sent to the status sensor; used to suppress duplicate publishes
  // when status_name() is called on every loop() tick.
  std::string last_published_status_;

  // -- Safety: over-temperature cutoff (P0-1 / P0-3) ------------------------
  IHeater *over_temp_sensor_{nullptr};       // sensor to monitor for cutoff
  float over_temp_limit_{165.0f};            // °C — hard cutoff threshold
  bool over_temp_cutoff_triggered_{false};   // latched; cleared only by reboot

  // -- Safety: brew timeout (P0-4) ------------------------------------------
  uint32_t brew_timeout_ms_{0};   // 0 = disabled
  uint32_t brew_start_ms_{0};     // millis() when brew_start() was called

  // -- Flush state (P2-2) ---------------------------------------------------
  float flush_volume_ml_{0.0f};   // target volume for current maintenance flush

  // -- Cleanup callbacks (P2-1) ---------------------------------------------
  std::function<void()> brew_cleanup_fn_;   // invoked in brew DONE→CLEANUP
  std::function<void()> steam_cleanup_fn_;  // invoked in steam CLEANUP

  // -- Internal helpers ------------------------------------------------------
  void advance_brew_();
  void advance_steam_();
  void advance_flush_();
  void enter_brewing_();
  void safe_stop_all_();
  // Publishes the current detailed status string to the status text sensor (if wired).
  // Call immediately after every brew/steam/flush state transition.
  void publish_status_();
  // Returns true if an over-temperature or sensor-fault cutoff was triggered.
  // Called at the top of loop() before advancing any state machine.
  bool check_over_temp_safety_();
};

}  // namespace espresso_machine
}  // namespace esphome

// ---------------------------------------------------------------------------
// FlushAction — ESPHome automation action for espresso_machine.flush (P2-2)
// Usage in YAML:
//   - espresso_machine.flush:
//       id: my_espresso
//       volume_ml: 50ml
// ---------------------------------------------------------------------------
namespace esphome {
namespace espresso_machine {

template<typename... Ts>
class FlushAction : public Action<Ts...> {
 public:
  void set_parent(EspressoMachine *parent) { parent_ = parent; }
  TEMPLATABLE_VALUE(float, volume_ml)
  void play(Ts... x) override { this->parent_->flush(this->volume_ml_.value(x...)); }

 private:
  EspressoMachine *parent_{nullptr};
};

}  // namespace espresso_machine
}  // namespace esphome
