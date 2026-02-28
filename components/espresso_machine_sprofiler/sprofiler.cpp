#include "sprofiler.h"
#include <cstdio>
#include <cstring>

namespace esphome {
namespace espresso_machine_sprofiler {

static const char *const TAG = "sprofiler";

// ---------------------------------------------------------------------------
// ESPHome lifecycle
// ---------------------------------------------------------------------------

void SprofilerShotUpload::setup() {
  ESP_LOGI(TAG, "Sprofiler shot upload initialised (server: %s)", server_url_.c_str());
}

void SprofilerShotUpload::loop() {
  // Attempt to upload any pending shot each loop tick.
  // upload_pending_shot() is a no-op when there is nothing to send.
  if (shot_pending_upload_) {
    upload_pending_shot();
  }
}

// ---------------------------------------------------------------------------
// Shot recording
// ---------------------------------------------------------------------------

void SprofilerShotUpload::begin_shot() {
  datapoints_.clear();
  shot_id_++;
  shot_duration_ms_ = 0;
  shot_pending_upload_ = false;
  recording_ = true;
  // Use millis() / 1000 as a rough Unix-epoch proxy on the device.
  // A real implementation could use an NTP-synced clock.
  shot_timestamp_ = millis() / 1000;
  ESP_LOGI(TAG, "Shot %u recording started", shot_id_);
}

void SprofilerShotUpload::add_datapoint(float time_sec, float pressure_bar,
                                        float temperature_c, float flow_ml_s,
                                        float weight_g) {
  if (!recording_)
    return;
  datapoints_.push_back({time_sec, pressure_bar, temperature_c, flow_ml_s, weight_g});
}

void SprofilerShotUpload::end_shot(uint32_t duration_ms) {
  if (!recording_)
    return;
  recording_ = false;
  shot_duration_ms_ = duration_ms;
  shot_pending_upload_ = true;
  ESP_LOGI(TAG, "Shot %u ended (%u ms, %zu datapoints)",
           shot_id_, duration_ms, datapoints_.size());
}

// ---------------------------------------------------------------------------
// JSON serialisation — Gaggiuino-compatible shot format
// ---------------------------------------------------------------------------

// Helper: append a float formatted to 2 decimal places.
static void append_float(std::string &out, float val) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(val));
  out += buf;
}

std::string SprofilerShotUpload::serialize_shot_json() const {
  if (datapoints_.empty())
    return {};

  // Pre-reserve a rough estimate: ~64 bytes per datapoint + 256 for header.
  std::string json;
  json.reserve(256 + datapoints_.size() * 64);

  json += "{\"id\":";
  json += std::to_string(shot_id_);
  json += ",\"timestamp\":";
  json += std::to_string(shot_timestamp_);
  json += ",\"duration\":";
  json += std::to_string(shot_duration_ms_);
  json += ",\"profile\":{\"name\":\"";
  // Escape double quotes in profile name.
  for (char c : profile_name_) {
    if (c == '"')
      json += "\\\"";
    else
      json += c;
  }
  json += "\"},\"datapoints\":[";

  for (size_t i = 0; i < datapoints_.size(); i++) {
    if (i > 0)
      json += ',';
    const auto &dp = datapoints_[i];
    json += "{\"time\":";
    append_float(json, dp.time_sec);
    json += ",\"pressure\":";
    append_float(json, dp.pressure_bar);
    json += ",\"temperature\":";
    append_float(json, dp.temperature_c);
    json += ",\"flow\":";
    append_float(json, dp.flow_ml_s);
    json += ",\"weight\":";
    append_float(json, dp.weight_g);
    json += '}';
  }

  json += "]}";
  return json;
}

// ---------------------------------------------------------------------------
// Upload
// ---------------------------------------------------------------------------

bool SprofilerShotUpload::upload_pending_shot() {
  if (!shot_pending_upload_)
    return false;

  std::string json = serialize_shot_json();
  if (json.empty()) {
    shot_pending_upload_ = false;
    return false;
  }

  std::string url = server_url_ + "/api/shots/upload";
  std::string auth = "Bearer " + api_token_;

  ESP_LOGI(TAG, "Uploading shot %u to %s (%zu bytes)",
           shot_id_, url.c_str(), json.size());

  int code = http_post(url, auth, json);
  if (code >= 200 && code < 300) {
    ESP_LOGI(TAG, "Shot %u uploaded successfully (HTTP %d)", shot_id_, code);
    shot_pending_upload_ = false;
    return true;
  }

  ESP_LOGW(TAG, "Shot %u upload failed (HTTP %d); will retry", shot_id_, code);
  return false;
}

int SprofilerShotUpload::http_post(const std::string &url,
                                   const std::string &auth_header,
                                   const std::string &body) {
  // Base implementation: log the attempt but do not perform network I/O.
  // On a real ESP32 this would be overridden or wired to ESPHome's HTTP
  // request component.  The virtual method enables clean unit-test mocking.
  ESP_LOGW(TAG, "http_post not wired — upload skipped (%zu bytes to %s)",
           body.size(), url.c_str());
  return -1;
}

}  // namespace espresso_machine_sprofiler
}  // namespace esphome
