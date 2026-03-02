#pragma once
#include <cstdio>

// Minimal DisplayBuffer mock for unit tests.
// Tracks update() calls and auto_clear state so tests can assert on them.
namespace esphome {
namespace display {

struct Color {};
static const Color COLOR_ON{};
static const Color COLOR_OFF{};

enum class TextAlign { TOP_LEFT = 0 };

class BaseFont {
 public:
  virtual ~BaseFont() = default;
};

class DisplayBuffer {
 public:
  int update_count{0};
  bool auto_clear_enabled{true};

  void clear() {}
  void filled_rectangle(int /*x*/, int /*y*/, int /*w*/, int /*h*/) {}
  void rectangle(int /*x*/, int /*y*/, int /*w*/, int /*h*/) {}
  void horizontal_line(int /*x*/, int /*y*/, int /*w*/) {}

  // Overloads used by display_ui.cpp — all no-op in tests.
  void print(int /*x*/, int /*y*/, const char * /*text*/) {}
  void print(int /*x*/, int /*y*/, BaseFont * /*font*/, const char * /*text*/) {}
  void print(int /*x*/, int /*y*/, BaseFont * /*font*/, Color /*color*/,
             TextAlign /*align*/, const char * /*text*/) {}

  // update() is called by ESPHome's scheduler; our loop() must NOT call it.
  void update() { update_count++; }

  void set_auto_clear(bool v) { auto_clear_enabled = v; }
};

}  // namespace display
}  // namespace esphome
