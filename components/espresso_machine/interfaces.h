#pragma once

// Pure-virtual interfaces used by the EspressoMachine orchestrator.
// Defined here so the orchestrator does not need to include sibling component
// headers, while still allowing unit-testable dependency injection.

namespace esphome {
namespace espresso_machine {

// ---------------------------------------------------------------------------
// IValve — solenoid valve abstraction
// ---------------------------------------------------------------------------
class IValve {
 public:
  virtual void open() = 0;
  virtual void close() = 0;
  virtual bool is_open() const = 0;
  virtual ~IValve() = default;
};

// ---------------------------------------------------------------------------
// IPump — vibration pump abstraction
//
// The pump owns the flow subsystem: it holds a reference to whatever flow
// sensor is physically attached and exposes flow data through this interface.
// This keeps the orchestrator independent of the flow meter type — the same
// interface works for vibration pumps, rotary pumps, or any future variant.
// ---------------------------------------------------------------------------
class IPump {
 public:
  virtual void turn_on() = 0;
  virtual void turn_off() = 0;
  virtual bool is_running() const = 0;

  // Flow subsystem — implemented by pumps that have a flow meter wired.
  // Default implementations return safe no-op values so pumps without a
  // flow meter compile and behave correctly out of the box.
  virtual float get_flow_rate() const { return 0.0f; }
  virtual float get_flow_total() const { return 0.0f; }
  virtual void reset_flow() {}

  // Bypass mode — when true, pump is flowing through an open valve (steam/purge)
  // rather than through a puck. In bypass mode there is no resistance, so flow
  // is at maximum and pressure stays near zero.
  // Default no-op for pumps that don't model this. MockPump implements this.
  virtual void set_bypass_mode(bool bypass) { (void)bypass; }

  virtual ~IPump() = default;
};

// ---------------------------------------------------------------------------
// IFlowMeter — pulse-counting volumetric sensor abstraction
// ---------------------------------------------------------------------------
class IFlowMeter {
 public:
  virtual float get_rate() const = 0;
  virtual float get_total_volume() const = 0;
  virtual void reset() = 0;
  virtual ~IFlowMeter() = default;
};

// ---------------------------------------------------------------------------
// IHeater — heater controller abstraction
//
// Allows the orchestrator to read current temperature and command a target
// setpoint without depending on the concrete heater implementation (PID
// climate, bang-bang, mock, etc.).  When no IHeater is wired the steam state
// machine falls back to immediate transitions, preserving backward compat.
// ---------------------------------------------------------------------------
class IHeater {
 public:
  virtual float get_current_temperature() const = 0;
  virtual void set_target_temperature(float t) = 0;
  // Called by the orchestrator's hard over-temperature safety cutoff.
  // Implementations should immediately disable the heater output (e.g. set
  // PID to off mode).  Default is a no-op so existing implementations that
  // don't need it remain compilable without changes.
  virtual void force_off() {}
  virtual ~IHeater() = default;
};

// ---------------------------------------------------------------------------
// IFlowObserver — receives flow rate updates (used by mock heater to model
// thermoblock cooling when water is flowing through the machine)
// ---------------------------------------------------------------------------
class IFlowObserver {
 public:
  virtual void set_flow_rate(float flow_ml_s) = 0;
  virtual ~IFlowObserver() = default;
};

}  // namespace espresso_machine
}  // namespace esphome
