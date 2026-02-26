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

  // Flow-rate control: set the target flow rate that the pump should maintain.
  // The pump modulates turn_on()/turn_off() (bang-bang) in its own loop() to
  // achieve this rate, respecting its min_on_ms / min_off_ms constraints.
  // Set to 0 to disable flow control (default: off). This keeps the orchestrator
  // free of bang-bang logic — it simply declares the desired flow and the pump
  // provides it. PumpSwitch and MockPump both implement this.
  virtual void set_target_flow(float ml_per_s) { (void)ml_per_s; }

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
  // Returns true when the current temperature is within acceptable range of
  // the target, meaning the machine may safely begin or continue operation.
  // The DEFAULT implementation checks only the lower bound (current >= target),
  // i.e. it does NOT reject temperatures above the target.  Concrete implementations
  // (e.g. EspressoMachineHeater) override this with a bidirectional tolerance band
  // (target - tol ≤ current ≤ target + tol) so that: (a) a thermoblock stabilised
  // slightly below the setpoint (e.g. 89.9°C at 90.0°C) is considered ready without
  // waiting indefinitely, and (b) the COOLING state correctly stays blocked while
  // the temperature is still above the target + tolerance.
  virtual bool is_ready(float target_temp) const {
    return get_current_temperature() >= target_temp;
  }
  // Returns true when the current temperature is significantly above the target,
  // indicating that active cooling (pump + purge valve) is needed before brewing.
  // The default uses a strict > comparison.  Concrete implementations may override
  // to apply an upper tolerance, so minor overshoot (e.g. 90.3°C at a 90.0°C
  // target with 0.5°C tolerance) does not unnecessarily trigger cooldown.
  virtual bool is_above_target(float target_temp) const {
    return get_current_temperature() > target_temp;
  }
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
