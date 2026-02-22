#pragma once
#include <cstdint>

// Controllable time for deterministic tests
extern uint32_t g_mock_millis;

inline uint32_t millis() { return g_mock_millis; }

// IRAM_ATTR is ESP-IDF/Arduino specific; no-op in test builds
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

// gpio namespace — interrupt types used by InternalGPIOPin::attach_interrupt
namespace gpio {
enum InterruptType {
  INTERRUPT_RISING_EDGE,
  INTERRUPT_FALLING_EDGE,
  INTERRUPT_ANY_EDGE,
};
}  // namespace gpio

// Minimal GPIO pin mock — records last written state, readable in tests
class GPIOPin {
 public:
  void setup() {}
  void digital_write(bool value) { state_ = value; }
  bool digital_read() const { return state_; }
  bool state_{false};

  // Interrupt attachment is a no-op in tests; pulses are injected via add_pulses()
  template<typename T>
  void attach_interrupt(void (*func)(T *), T *arg, gpio::InterruptType type) {}
};

// InternalGPIOPin supports interrupt attachment; in tests it is identical to GPIOPin
using InternalGPIOPin = GPIOPin;
