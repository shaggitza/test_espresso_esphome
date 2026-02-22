#pragma once

// Minimal automation stubs for unit tests.
// Action template classes compile but are not exercised in tests;
// pulses / calibration are driven directly via FlowMeter methods.

namespace esphome {

template<typename... Ts>
class Action {
 public:
  virtual void play(Ts... x) = 0;
  virtual ~Action() = default;
};

}  // namespace esphome

// TemplateableValue<T> — holds a static value; template play() args are ignored.
template<typename T>
struct TemplateableValue {
  T val_{};
  TemplateableValue() = default;
  explicit TemplateableValue(T v) : val_(v) {}
  template<typename... Us>
  T value(Us... /*unused*/) const { return val_; }
};

// Simplified TEMPLATABLE_VALUE macro that compiles under g++ without ESP-IDF.
#define TEMPLATABLE_VALUE(type, name)                   \
 protected:                                              \
  TemplateableValue<type> name##_{};                   \
                                                         \
 public:                                                 \
  void set_##name(type v) { name##_.val_ = v; }
