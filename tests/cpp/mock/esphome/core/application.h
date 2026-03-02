#pragma once

// Minimal Application stub for unit tests.
// App.safe_reboot() is called by the Restart action in the maintenance menu;
// it is a no-op in tests.
namespace esphome {

class Application {
 public:
  void safe_reboot() {}
};

static Application App;

}  // namespace esphome
