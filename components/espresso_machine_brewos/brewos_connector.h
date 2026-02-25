#pragma once

#include <cstdint>
#include <string>
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

// Forward-declare EspressoMachine so the header can be included without
// pulling in the full orchestrator header (avoids circular includes and
// speeds up compilation in production builds).
namespace esphome {
namespace espresso_machine {
class EspressoMachine;
}  // namespace espresso_machine
}  // namespace esphome

namespace esphome {
namespace espresso_machine_brewos {

// ---------------------------------------------------------------------------
// BrewOSConnector — connects the ESPHome espresso machine to the BrewOS
// cloud relay service via a persistent WebSocket connection.
//
// YAML configuration:
//   espresso_machine_brewos:
//     url: https://cloud.brewos.io    # BrewOS cloud base URL
//     espresso_machine: my_espresso   # id of the espresso_machine: block
//
// Behaviour:
//   • Derives a device ID from the ESP32 chip MAC (format: BRW-XXXXXXXX).
//   • Generates and persists a 32-byte device key in NVS (first-boot only).
//   • Opens a WebSocket to <url>/ws/device?id=<id>&key=<key>.
//   • Sends a JSON status message to the cloud every STATUS_INTERVAL_MS ms.
//   • Dispatches commands received from the cloud to EspressoMachine.
//   • Reconnects automatically with exponential back-off on disconnect.
//
// Offline / local operation:
//   Cloud connectivity is entirely optional.  The machine operates normally
//   when the cloud is unreachable; commands can still be issued via the
//   native Home Assistant API.
//
// Supported cloud commands (JSON "type" field):
//   brew_start, brew_stop, steam_start, steam_stop,
//   machine_on, machine_off, flush (requires "volume_ml" field)
// ---------------------------------------------------------------------------
class BrewOSConnector : public Component {
 public:
  // Called by Python code-generation to wire the component.
  void set_espresso_machine(espresso_machine::EspressoMachine *machine) {
    machine_ = machine;
  }
  void set_url(const std::string &url) { url_ = url; }

  // ESPHome lifecycle — networking calls are guarded by #ifdef ARDUINO.
  void setup() override;
  void loop() override;

  // ---------------------------------------------------------------------------
  // Public methods — usable in tests without networking.
  // ---------------------------------------------------------------------------

  // Dispatch a JSON command message received from the BrewOS cloud.
  // Supported "type" values: brew_start, brew_stop, steam_start, steam_stop,
  //                           machine_on, machine_off, flush
  void on_message(const std::string &json);

  // Serialise the current machine state into the pico_status JSON that is
  // periodically sent to the cloud.  Public so tests can validate the output.
  std::string build_status_json() const;

  // True when the WebSocket is currently connected to the cloud service.
  bool is_connected() const { return connected_; }

 protected:
  // Send a raw JSON string over the WebSocket (no-op when not connected or
  // when compiled without ARDUINO).  Separated from on_message() so the
  // command-dispatch path can be unit-tested without networking.
  void send_raw(const std::string &json);

 private:
  espresso_machine::EspressoMachine *machine_{nullptr};
  std::string url_;
  bool connected_{false};
  uint32_t last_status_ms_{0};

  // Status update interval — matches BrewOS recommended 2–5 s cadence.
  static constexpr uint32_t STATUS_INTERVAL_MS = 5000;
};

}  // namespace espresso_machine_brewos
}  // namespace esphome
