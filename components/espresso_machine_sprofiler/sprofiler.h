#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

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
// Design:
//   - begin_shot() starts recording; end_shot() finalises the shot record.
//   - add_datapoint() is called during the brew (typically at ~10 Hz).
//   - serialize_shot_json() produces a Gaggiuino-compatible JSON string.
//   - upload_pending_shot() POSTs the JSON to the configured server.
//   - http_post() is virtual so unit tests can mock the network layer.
// ---------------------------------------------------------------------------
class SprofilerShotUpload : public Component {
 public:
  // ----- Configuration setters (called by Python codegen) ------------------
  void set_server_url(const std::string &url) { server_url_ = url; }
  void set_api_token(const std::string &token) { api_token_ = token; }
  void set_profile_name(const std::string &name) { profile_name_ = name; }

  // ----- Shot recording ----------------------------------------------------
  // Call begin_shot() when the brew starts.  Clears any previous datapoints
  // and increments the shot counter.
  void begin_shot();

  // Record a single telemetry sample.  Ignored if not currently recording.
  void add_datapoint(float time_sec, float pressure_bar,
                     float temperature_c, float flow_ml_s, float weight_g);

  // Finalise the shot record.  `duration_ms` is the total brew time.
  // After this call the shot is ready for upload.
  void end_shot(uint32_t duration_ms);

  // ----- Serialisation -----------------------------------------------------
  // Produces a Gaggiuino-compatible JSON string for the current shot record.
  // Returns an empty string if no shot is pending.
  std::string serialize_shot_json() const;

  // ----- Upload ------------------------------------------------------------
  // Returns true if a shot has been recorded but not yet uploaded.
  bool has_pending_upload() const { return shot_pending_upload_; }

  // Attempt to upload the pending shot.  Returns true on success (HTTP 2xx).
  // On failure the shot stays pending for a future retry.
  bool upload_pending_shot();

  // Virtual HTTP POST — override in tests to avoid real network calls.
  // Returns the HTTP status code (e.g. 200, 201) or a negative value on
  // transport-level failure.
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

 protected:
  // -- Configuration ---------------------------------------------------------
  std::string server_url_{"https://sprofiler.io"};
  std::string api_token_;
  std::string profile_name_{"Manual"};

  // -- Shot state ------------------------------------------------------------
  uint32_t shot_id_{0};
  uint32_t shot_timestamp_{0};
  uint32_t shot_duration_ms_{0};
  std::vector<ShotDatapoint> datapoints_;
  bool recording_{false};
  bool shot_pending_upload_{false};
};

}  // namespace espresso_machine_sprofiler
}  // namespace esphome
