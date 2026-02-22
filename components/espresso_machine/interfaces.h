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
// ---------------------------------------------------------------------------
class IPump {
 public:
  virtual void turn_on() = 0;
  virtual void turn_off() = 0;
  virtual bool is_running() const = 0;
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
// IOrchestrator — exposes machine-busy state for grinder lockout
// ---------------------------------------------------------------------------
class IOrchestrator {
 public:
  virtual bool is_busy() const = 0;
  virtual ~IOrchestrator() = default;
};

}  // namespace espresso_machine
}  // namespace esphome
