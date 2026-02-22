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

}  // namespace espresso_machine
}  // namespace esphome
