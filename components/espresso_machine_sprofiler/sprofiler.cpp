#include "sprofiler.h"
#include "components/espresso_machine/espresso_machine.h"
#include <cstdio>
#include <cstring>

namespace esphome {
namespace espresso_machine_sprofiler {

static const char *const TAG = "sprofiler";

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void SprofilerShotUpload::set_server_url(const std::string &url) {
  server_url_ = url;
  // Normalise: strip trailing slashes to avoid double-slash in URL paths.
  while (!server_url_.empty() && server_url_.back() == '/')
    server_url_.pop_back();
}

// ---------------------------------------------------------------------------
// ESPHome lifecycle
// ---------------------------------------------------------------------------

void SprofilerShotUpload::setup() {
  ESP_LOGI(TAG, "Sprofiler shot upload initialised (server: %s)", server_url_.c_str());
}

void SprofilerShotUpload::loop() {
  // --- Auto-recording: observe orchestrator state --------------------------
  if (machine_) {
    bool is_brewing =
        machine_->get_mode() == espresso_machine::EspressoMode::BREWING;

    // Rising edge: orchestrator entered BREWING mode → start recording.
    if (is_brewing && !was_brewing_) {
      begin_shot();
      auto_record_start_ms_ = millis();
      last_sample_ms_ = millis();
    }

    // While brewing: sample telemetry at ~10 Hz.
    if (is_brewing && recording_) {
      uint32_t now = millis();
      if (now - last_sample_ms_ >= SAMPLE_INTERVAL_MS) {
        float elapsed_s = (now - auto_record_start_ms_) / 1000.0f;
        add_datapoint(elapsed_s, 0.0f,
                      machine_->get_brew_temperature(),
                      machine_->get_brew_flow_rate(), 0.0f);
        last_sample_ms_ = now;
      }
    }

    // Falling edge: orchestrator left BREWING mode → finalise shot.
    if (!is_brewing && was_brewing_ && recording_) {
      uint32_t duration = millis() - auto_record_start_ms_;
      end_shot(duration);
    }

    was_brewing_ = is_brewing;
  }

  // --- Upload any pending shot ---------------------------------------------
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

static void append_float(std::string &out, float val) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.2f", val);
  out += buf;
}

std::string SprofilerShotUpload::serialize_shot_json() const {
  if (datapoints_.empty())
    return {};

  std::string json;
  json.reserve(256 + datapoints_.size() * 64);

  json += "{\"id\":";
  json += std::to_string(shot_id_);
  json += ",\"timestamp\":";
  json += std::to_string(shot_timestamp_);
  json += ",\"duration\":";
  json += std::to_string(shot_duration_ms_);
  json += ",\"profile\":{\"name\":\"";
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
  ESP_LOGW(TAG, "http_post not wired — upload skipped (%zu bytes to %s)",
           body.size(), url.c_str());
  return -1;
}

}  // namespace espresso_machine_sprofiler
}  // namespace esphome
