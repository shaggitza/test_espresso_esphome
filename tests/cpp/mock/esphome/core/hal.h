#pragma once
#include <cstdint>

// Controllable time for deterministic tests
extern uint32_t g_mock_millis;

inline uint32_t millis() { return g_mock_millis; }

// Minimal GPIO pin mock — records last written state, readable in tests
class GPIOPin {
 public:
  void setup() {}
  void digital_write(bool value) { state_ = value; }
  bool digital_read() const { return state_; }
  bool state_{false};
};
