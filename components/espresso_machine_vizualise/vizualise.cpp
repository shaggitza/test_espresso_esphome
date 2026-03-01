#include "vizualise.h"
#include "esphome/components/espresso_machine/espresso_machine.h"
#include "esphome/components/http_request/http_request.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace esphome {
namespace espresso_machine_vizualise {

static const char *const TAG = "vizualise";

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void VizualiseShotUpload::set_server_url(const std::string &url) {
  server_url_ = url;
  // Normalise: strip trailing slashes to avoid double-slash in URL paths.
  while (!server_url_.empty() && server_url_.back() == '/')
    server_url_.pop_back();
}

// ---------------------------------------------------------------------------
// ESPHome lifecycle
// ---------------------------------------------------------------------------

void VizualiseShotUpload::setup() {
  ESP_LOGI(TAG, "Vizualise shot upload initialised (server: %s)", server_url_.c_str());
}

void VizualiseShotUpload::loop() {
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
        float elapsed_ms = static_cast<float>(now - auto_record_start_ms_);
        add_datapoint(elapsed_ms, 0.0f,
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

  // --- Upload any pending shot (with retry backoff) -------------------------
  if (shot_pending_upload_) {
    uint32_t now = millis();
    if (upload_retry_count_ == 0 ||
        (now - last_upload_attempt_ms_) >= upload_retry_interval_ms_) {
      if (upload_retry_count_ < UPLOAD_MAX_RETRIES) {
        last_upload_attempt_ms_ = now;
        if (!upload_pending_shot()) {
          upload_retry_count_++;
          if (upload_retry_interval_ms_ == 0)
            upload_retry_interval_ms_ = UPLOAD_INITIAL_RETRY_MS;
          else
            upload_retry_interval_ms_ = std::min(
                upload_retry_interval_ms_ * 2, UPLOAD_MAX_RETRY_MS);
          ESP_LOGD(TAG, "Upload retry %u scheduled in %u ms",
                   upload_retry_count_, upload_retry_interval_ms_);
        }
      } else {
        ESP_LOGW(TAG, "Shot %u upload abandoned after %u retries",
                 shot_id_, UPLOAD_MAX_RETRIES);
        shot_pending_upload_ = false;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Shot recording
// ---------------------------------------------------------------------------

void VizualiseShotUpload::begin_shot() {
  time_ms_.clear();
  pressure_bar_.clear();
  temperature_c_.clear();
  flow_ml_s_.clear();
  weight_g_.clear();
  // Pre-reserve for a typical 30-second shot at 10 Hz (300 datapoints).
  time_ms_.reserve(EXPECTED_DATAPOINTS_PER_SHOT);
  pressure_bar_.reserve(EXPECTED_DATAPOINTS_PER_SHOT);
  temperature_c_.reserve(EXPECTED_DATAPOINTS_PER_SHOT);
  flow_ml_s_.reserve(EXPECTED_DATAPOINTS_PER_SHOT);
  weight_g_.reserve(EXPECTED_DATAPOINTS_PER_SHOT);
  shot_id_++;
  shot_duration_ms_ = 0;
  shot_pending_upload_ = false;
  upload_retry_count_ = 0;
  upload_retry_interval_ms_ = 0;
  recording_ = true;
  shot_timestamp_ = millis() / 1000;
  ESP_LOGI(TAG, "Shot %u recording started", shot_id_);
}

void VizualiseShotUpload::add_datapoint(float time_ms, float pressure_bar,
                                        float temperature_c, float flow_ml_s,
                                        float weight_g) {
  if (!recording_)
    return;
  time_ms_.push_back(time_ms);
  pressure_bar_.push_back(pressure_bar);
  temperature_c_.push_back(temperature_c);
  flow_ml_s_.push_back(flow_ml_s);
  weight_g_.push_back(weight_g);
}

void VizualiseShotUpload::end_shot(uint32_t duration_ms) {
  if (!recording_)
    return;
  recording_ = false;
  shot_duration_ms_ = duration_ms;
  shot_pending_upload_ = true;
  ESP_LOGI(TAG, "Shot %u ended (%u ms, %zu datapoints)",
           shot_id_, duration_ms, time_ms_.size());
}

// ---------------------------------------------------------------------------
// JSON serialisation — visualizer.coffee parallel-arrays format
// ---------------------------------------------------------------------------

static void append_float(std::string &out, float val) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.2f", val);
  out += buf;
}

static void append_float_array(std::string &out, const std::vector<float> &arr) {
  out += '[';
  for (size_t i = 0; i < arr.size(); i++) {
    if (i > 0)
      out += ',';
    append_float(out, arr[i]);
  }
  out += ']';
}

static void append_escaped_string(std::string &out, const std::string &s) {
  for (char c : s) {
    if (c == '"')
      out += "\\\"";
    else
      out += c;
  }
}

std::string VizualiseShotUpload::serialize_shot_json() const {
  if (time_ms_.empty())
    return {};

  std::string json;
  json.reserve(256 + time_ms_.size() * 40);

  json += "{\"start_time\":";
  json += std::to_string(shot_timestamp_);
  json += ",\"machine\":\"";
  append_escaped_string(json, machine_name_);
  json += "\",\"profile\":{\"name\":\"";
  append_escaped_string(json, profile_name_);
  json += "\"},\"sample_interval\":";
  json += std::to_string(SAMPLE_INTERVAL_MS);
  json += ",\"data\":{\"time\":";
  append_float_array(json, time_ms_);
  json += ",\"pressure\":";
  append_float_array(json, pressure_bar_);
  json += ",\"temperature\":";
  append_float_array(json, temperature_c_);
  json += ",\"flow\":";
  append_float_array(json, flow_ml_s_);
  json += ",\"weight\":";
  append_float_array(json, weight_g_);
  json += "}}";
  return json;
}

// ---------------------------------------------------------------------------
// Upload
// ---------------------------------------------------------------------------

bool VizualiseShotUpload::upload_pending_shot() {
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

int VizualiseShotUpload::http_post(const std::string &url,
                                   const std::string &auth_header,
                                   const std::string &body) {
  if (!http_request_) {
    ESP_LOGW(TAG, "http_request not configured — upload skipped (%zu bytes to %s)",
             body.size(), url.c_str());
    return -1;
  }

  std::list<esphome::http_request::Header> headers = {
    {"Authorization", auth_header},
    {"Content-Type", "application/json"},
  };

  auto container = http_request_->start(url, "POST", body, headers);
  if (!container) {
    ESP_LOGW(TAG, "HTTP connection failed for %s", url.c_str());
    return -1;
  }

  int status = container->status_code;
  container->end();
  return status;
}

}  // namespace espresso_machine_vizualise
}  // namespace esphome
