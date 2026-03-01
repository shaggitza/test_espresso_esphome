#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

// Forward-declare the orchestrator (avoids circular header include).
namespace esphome {
namespace espresso_machine {
class EspressoMachine;
}  // namespace espresso_machine
}  // namespace esphome

// Forward-declare HttpRequestComponent (full header only needed in .cpp).
namespace esphome {
namespace http_request {
class HttpRequestComponent;
}  // namespace http_request
}  // namespace esphome

namespace esphome {
namespace espresso_machine_vizualise {

// ---------------------------------------------------------------------------
// VizualiseShotUpload — collects shot telemetry and uploads to visualizer.coffee
//
// When wired to an EspressoMachine orchestrator via set_machine(), the
// component automatically:
//   - Starts recording when the orchestrator enters BREWING mode.
//   - Samples temperature and flow at ~10 Hz from the orchestrator's sensors.
//   - Ends recording and triggers upload when the orchestrator leaves BREWING.
//
// Manual recording via begin_shot() / add_datapoint() / end_shot() is still
// supported for use without the orchestrator.
//
// The shot is serialised in the visualizer.coffee parallel-arrays JSON format
// (compatible with Decent Espresso and Gaggiuino uploads):
//
//   {
//     "start_time": <unix_timestamp>,
//     "machine":    "<machine_name>",
//     "profile":    { "name": "<profile_name>" },
//     "sample_interval": 100,
//     "data": {
//       "time":        [ms, ms, ...],
//       "pressure":    [bar, ...],
//       "temperature": [°C, ...],
//       "flow":        [ml/s, ...],
//       "weight":      [g, ...]
//     }
//   }
// ---------------------------------------------------------------------------
class VizualiseShotUpload : public Component {
 public:
  // ----- Configuration setters (called by Python codegen) ------------------
  void set_server_url(const std::string &url);
  void set_api_token(const std::string &token) { api_token_ = token; }
  void set_profile_name(const std::string &name) { profile_name_ = name; }
  void set_machine_name(const std::string &name) { machine_name_ = name; }

  // ----- Orchestrator reference (first-class citizen wiring) ---------------
  // When set, the component observes the orchestrator's brew state and
  // automatically records + uploads shots.
  void set_machine(espresso_machine::EspressoMachine *m) { machine_ = m; }

  // ----- HTTP transport (injected from Python codegen) ----------------------
  // When set, http_post() uses this component to make real HTTPS requests.
  void set_http_request(http_request::HttpRequestComponent *req) { http_request_ = req; }

  // ----- Shot recording (manual API) ---------------------------------------
  void begin_shot();
  void add_datapoint(float time_ms, float pressure_bar,
                     float temperature_c, float flow_ml_s, float weight_g);
  void end_shot(uint32_t duration_ms);

  // ----- Serialisation (visualizer.coffee parallel-arrays JSON format) ------
  std::string serialize_shot_json() const;

  // ----- Upload ------------------------------------------------------------
  bool has_pending_upload() const { return shot_pending_upload_; }
  bool upload_pending_shot();

  // Virtual HTTP POST — override in tests to avoid real network calls.
  virtual int http_post(const std::string &url,
                        const std::string &auth_header,
                        const std::string &body);

  // ----- Accessors (mainly for tests) --------------------------------------
  size_t get_datapoint_count() const { return time_ms_.size(); }
  uint32_t get_shot_id() const { return shot_id_; }
  uint32_t get_shot_duration_ms() const { return shot_duration_ms_; }
  bool is_recording() const { return recording_; }
  const std::string &get_server_url() const { return server_url_; }
  const std::string &get_api_token() const { return api_token_; }
  const std::string &get_profile_name() const { return profile_name_; }
  const std::string &get_machine_name() const { return machine_name_; }

  // ----- ESPHome lifecycle --------------------------------------------------
  void setup() override;
  void loop() override;

  // ----- Constants (used by tests) ------------------------------------------
  static constexpr uint32_t UPLOAD_INITIAL_RETRY_MS = 5000;   // 5 s
  static constexpr uint32_t UPLOAD_MAX_RETRY_MS = 300000;     // 5 min
  static constexpr uint8_t  UPLOAD_MAX_RETRIES = 10;
  static constexpr size_t EXPECTED_DATAPOINTS_PER_SHOT = 300;
  static constexpr uint32_t SAMPLE_INTERVAL_MS = 100;         // ~10 Hz

 protected:
  // -- Configuration ---------------------------------------------------------
  std::string server_url_{"https://visualizer.coffee"};
  std::string api_token_;
  std::string profile_name_{"Manual"};
  std::string machine_name_{"ESPHome Espresso Machine"};

  // -- Orchestrator reference ------------------------------------------------
  espresso_machine::EspressoMachine *machine_{nullptr};

  // -- HTTP transport --------------------------------------------------------
  http_request::HttpRequestComponent *http_request_{nullptr};

  // -- Shot state — parallel arrays (visualizer.coffee format) ---------------
  uint32_t shot_id_{0};
  uint32_t shot_timestamp_{0};
  uint32_t shot_duration_ms_{0};
  std::vector<float> time_ms_;        // ms since shot start
  std::vector<float> pressure_bar_;   // brew pressure (0 if no sensor)
  std::vector<float> temperature_c_;  // water temperature in °C
  std::vector<float> flow_ml_s_;      // water flow rate in ml/s
  std::vector<float> weight_g_;       // cup weight in grams (0 if no scale)
  bool recording_{false};
  bool shot_pending_upload_{false};

  // -- Upload retry state ----------------------------------------------------
  uint32_t last_upload_attempt_ms_{0};
  uint32_t upload_retry_interval_ms_{0};
  uint8_t upload_retry_count_{0};

  // -- Auto-recording state --------------------------------------------------
  bool was_brewing_{false};
  uint32_t auto_record_start_ms_{0};
  uint32_t last_sample_ms_{0};
};

}  // namespace espresso_machine_vizualise
}  // namespace esphome
