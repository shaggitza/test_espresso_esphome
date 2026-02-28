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
namespace espresso_machine_sprofiler {

// ---------------------------------------------------------------------------
// ShotDatapoint — single telemetry sample recorded during a brew
// ---------------------------------------------------------------------------
struct ShotDatapoint {
  float time_sec;        // seconds since shot start
  float pressure_bar;    // brew pressure (0 if no pressure sensor)
  float temperature_c;   // water temperature in °C
  float flow_ml_s;       // water flow rate in ml/s
  float weight_g;        // cup weight in grams (0 if no scale)
};

// ---------------------------------------------------------------------------
// SprofilerShotUpload — collects shot telemetry and uploads to Sprofiler cloud
//
// When wired to an EspressoMachine orchestrator via set_machine(), the
// component automatically:
//   - Starts recording when the orchestrator enters BREWING mode.
//   - Samples temperature and flow at ~10 Hz from the orchestrator's sensors.
//   - Ends recording and triggers upload when the orchestrator leaves BREWING.
//
// Manual recording via begin_shot() / add_datapoint() / end_shot() is still
// supported for use without the orchestrator.
// ---------------------------------------------------------------------------
class SprofilerShotUpload : public Component {
 public:
  // ----- Configuration setters (called by Python codegen) ------------------
  void set_server_url(const std::string &url);
  void set_api_token(const std::string &token) { api_token_ = token; }
  void set_profile_name(const std::string &name) { profile_name_ = name; }

  // ----- Orchestrator reference (first-class citizen wiring) ---------------
  // When set, the sprofiler observes the orchestrator's brew state and
  // automatically records + uploads shots.
  void set_machine(espresso_machine::EspressoMachine *m) { machine_ = m; }

  // ----- HTTP transport (injected from Python codegen) ----------------------
  // When set, http_post() uses this component to make real HTTPS requests.
  void set_http_request(http_request::HttpRequestComponent *req) { http_request_ = req; }

  // ----- Shot recording (manual API) ---------------------------------------
  void begin_shot();
  void add_datapoint(float time_sec, float pressure_bar,
                     float temperature_c, float flow_ml_s, float weight_g);
  void end_shot(uint32_t duration_ms);

  // ----- Serialisation -----------------------------------------------------
  std::string serialize_shot_json() const;

  // ----- Upload ------------------------------------------------------------
  bool has_pending_upload() const { return shot_pending_upload_; }
  bool upload_pending_shot();

  // Virtual HTTP POST — override in tests to avoid real network calls.
  virtual int http_post(const std::string &url,
                        const std::string &auth_header,
                        const std::string &body);

  // ----- Accessors (mainly for tests) --------------------------------------
  const std::vector<ShotDatapoint> &get_datapoints() const { return datapoints_; }
  uint32_t get_shot_id() const { return shot_id_; }
  uint32_t get_shot_duration_ms() const { return shot_duration_ms_; }
  bool is_recording() const { return recording_; }
  const std::string &get_server_url() const { return server_url_; }
  const std::string &get_api_token() const { return api_token_; }
  const std::string &get_profile_name() const { return profile_name_; }

  // ----- ESPHome lifecycle --------------------------------------------------
  void setup() override;
  void loop() override;

  // ----- Constants (used by tests) ------------------------------------------
  static constexpr uint32_t UPLOAD_INITIAL_RETRY_MS = 5000;   // 5 s
  static constexpr uint32_t UPLOAD_MAX_RETRY_MS = 300000;     // 5 min
  static constexpr uint8_t  UPLOAD_MAX_RETRIES = 10;
  static constexpr size_t EXPECTED_DATAPOINTS_PER_SHOT = 300;

 protected:
  // -- Configuration ---------------------------------------------------------
  std::string server_url_{"https://sprofiler.io"};
  std::string api_token_;
  std::string profile_name_{"Manual"};

  // -- Orchestrator reference ------------------------------------------------
  espresso_machine::EspressoMachine *machine_{nullptr};

  // -- HTTP transport --------------------------------------------------------
  http_request::HttpRequestComponent *http_request_{nullptr};

  // -- Shot state ------------------------------------------------------------
  uint32_t shot_id_{0};
  uint32_t shot_timestamp_{0};
  uint32_t shot_duration_ms_{0};
  std::vector<ShotDatapoint> datapoints_;
  bool recording_{false};
  bool shot_pending_upload_{false};

  // -- Upload retry state ----------------------------------------------------
  // Prevents blocking the loop with repeated HTTP requests on every tick.
  // After a failed upload, the next retry is delayed by upload_retry_interval_ms_
  // which doubles on each failure (exponential backoff) up to a cap.
  uint32_t last_upload_attempt_ms_{0};
  uint32_t upload_retry_interval_ms_{0};
  uint8_t upload_retry_count_{0};

  // -- Auto-recording state --------------------------------------------------
  bool was_brewing_{false};          // previous-tick brewing flag for edge detection
  uint32_t auto_record_start_ms_{0}; // millis() when auto-recording began
  uint32_t last_sample_ms_{0};       // millis() of last auto-sampled datapoint
  static constexpr uint32_t SAMPLE_INTERVAL_MS = 100;  // ~10 Hz
};

}  // namespace espresso_machine_sprofiler
}  // namespace esphome
