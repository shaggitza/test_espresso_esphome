#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
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
};

// ---------------------------------------------------------------------------
// Steam state machine
// ---------------------------------------------------------------------------
enum class SteamState : uint8_t {
  IDLE = 0,
  HEATING = 1,   // waiting for thermoblock to reach steam temperature
  STEAMING = 2,  // steam valve open; pump pulsing
  COOLING = 3,   // heater setpoint lowered; waiting for temp to drop
  CLEANUP = 4,   // purging steam path
};

// ---------------------------------------------------------------------------
// Top-level mode (used for grinder lockout, display, HA sensor)
// ---------------------------------------------------------------------------
enum class EspressoMode : uint8_t {
  IDLE = 0,
  BREWING = 1,
  STEAMING = 2,
};

// ---------------------------------------------------------------------------
// EspressoMachine — pure orchestrator, owns no hardware
// ---------------------------------------------------------------------------
class EspressoMachine : public Component {
 public:
  // ----- Brew hardware setters (called by Python codegen) -----------------
  // Heater is a native ESPHome climate entity; stored as Component* here.
  // Phase 2 will call set_target_temperature() via a climate::ClimateCall.
  void set_brew_heater(Component *h) { brew_heater_ = h; }
  void set_brew_valve(IValve *v) { brew_valve_ = v; }
  void set_brew_purge_valve(IValve *v) { brew_purge_valve_ = v; }
  void set_brew_pump(IPump *p) { brew_pump_ = p; }
  void set_brew_flow_meter(IFlowMeter *fm) { brew_flow_meter_ = fm; }

  // ----- Brew configuration setters ----------------------------------------
  void set_brew_target_temperature(float t) { brew_target_temp_ = t; }
  void set_brew_flow_max(float ml) { brew_flow_max_ml_ = ml; }
  void set_brew_flow_offset(float ml) { brew_flow_offset_ml_ = ml; }

  // ----- Steam hardware setters --------------------------------------------
  // Heater shared with brew; stored as Component* until Phase 2 climate wiring.
  void set_steam_heater(Component *h) { steam_heater_ = h; }
  void set_steam_valve(IValve *v) { steam_valve_ = v; }
  void set_steam_purge_valve(IValve *v) { steam_purge_valve_ = v; }
  void set_steam_pump(IPump *p) { steam_pump_ = p; }

  // ----- Steam configuration setters ---------------------------------------
  void set_steam_target_temperature(float t) { steam_target_temp_ = t; }
  void set_steam_flow_max(float ml_per_s) { steam_flow_max_ml_per_s_ = ml_per_s; }
  void set_steam_cool_down_to(float t) { steam_cool_down_to_ = t; }

  // ----- ESPHome lifecycle --------------------------------------------------
  void setup() override;
  void loop() override;

  // ----- Public actions (callable from YAML / HA automations) --------------
  void brew_start();
  void brew_stop();
  void steam_start();
  void steam_stop();

  // ----- Status accessors ---------------------------------------------------
  EspressoMode get_mode() const { return mode_; }
  const char *mode_name() const;
  BrewState get_brew_state() const { return brew_state_; }
  SteamState get_steam_state() const { return steam_state_; }

  // ----- Safety query for grinder lockout -----------------------------------
  bool is_busy() const { return mode_ != EspressoMode::IDLE; }

 protected:
  // -- Mode / state ---------------------------------------------------------
  EspressoMode mode_{EspressoMode::IDLE};
  BrewState brew_state_{BrewState::IDLE};
  SteamState steam_state_{SteamState::IDLE};

  // -- Brew hardware ---------------------------------------------------------
  Component *brew_heater_{nullptr};  // native ESPHome climate entity (Phase 2)
  IValve *brew_valve_{nullptr};
  IValve *brew_purge_valve_{nullptr};
  IPump *brew_pump_{nullptr};
  IFlowMeter *brew_flow_meter_{nullptr};

  // -- Brew config -----------------------------------------------------------
  float brew_target_temp_{90.0f};     // °C
  float brew_flow_max_ml_{40.0f};     // ml to extract before stopping
  float brew_flow_offset_ml_{20.0f};  // ml absorbed by puck (subtracted for yield)

  // -- Steam hardware --------------------------------------------------------
  Component *steam_heater_{nullptr};  // shared native climate entity (Phase 2)
  IValve *steam_valve_{nullptr};
  IValve *steam_purge_valve_{nullptr};
  IPump *steam_pump_{nullptr};

  // -- Steam config ----------------------------------------------------------
  float steam_target_temp_{135.0f};       // °C
  float steam_flow_max_ml_per_s_{2.0f};   // ml/s target flow rate while steaming
  float steam_cool_down_to_{90.0f};       // °C — heater setpoint after steaming

  // -- Internal state --------------------------------------------------------
  uint32_t state_entered_ms_{0};  // millis() when current brew/steam state was entered

  // -- Internal helpers ------------------------------------------------------
  void advance_brew_();
  void advance_steam_();
  void safe_stop_all_();
};

}  // namespace espresso_machine
}  // namespace esphome
