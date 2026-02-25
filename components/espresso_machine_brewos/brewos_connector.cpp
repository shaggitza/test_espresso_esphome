#include "brewos_connector.h"

#include <cstdio>
#include <cstring>
#include "esphome/core/defines.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
// Full orchestrator header — needed to call brew/steam methods.
#include "../espresso_machine/espresso_machine.h"

// ---------------------------------------------------------------------------
// ESP32 Arduino-only: WebSocket client.
// All networking code is wrapped in #ifdef USE_ESP32_FRAMEWORK_ARDUINO so the
// component can be compiled and unit-tested on the host without Arduino SDK.
// ---------------------------------------------------------------------------
#ifdef USE_ESP32_FRAMEWORK_ARDUINO
#include <Arduino.h>
#include <WiFi.h>  // Must be included before WebSocketsClient.h
#include <WebSocketsClient.h>

// Module-level WebSocket client — only one BrewOSConnector is expected.
static WebSocketsClient g_ws_client;
static bool g_ws_connected_flag = false;
static esphome::espresso_machine_brewos::BrewOSConnector *g_connector_ptr = nullptr;

// WebSocket event handler — called by the arduinoWebSockets library.
static void ws_event(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      g_ws_connected_flag = true;
      ESP_LOGI("brewos", "Connected to BrewOS cloud");
      break;
    case WStype_DISCONNECTED:
      g_ws_connected_flag = false;
      ESP_LOGI("brewos", "Disconnected from BrewOS cloud — will retry");
      break;
    case WStype_TEXT:
      if (g_connector_ptr != nullptr && payload != nullptr) {
        g_connector_ptr->on_message(std::string(reinterpret_cast<char *>(payload), length));
      }
      break;
    default:
      break;
  }
}

// Derive a device ID from the ESP32 chip MAC: "BRW-XXXXXXXX" (8 hex chars).
static std::string brewos_get_device_id() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[13];
  // XOR upper and lower 32-bit halves so we get a device-unique 32-bit value.
  uint32_t id32 = static_cast<uint32_t>(mac) ^ static_cast<uint32_t>(mac >> 32);
  snprintf(buf, sizeof(buf), "BRW-%08X", id32);
  return std::string(buf);
}

// Base64url alphabet (RFC 4648 §5, no padding).
static const char kB64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static std::string base64url_encode(const uint8_t *data, size_t len) {
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3) {
    uint32_t b = static_cast<uint32_t>(data[i]) << 16;
    if (i + 1 < len) b |= static_cast<uint32_t>(data[i + 1]) << 8;
    if (i + 2 < len) b |= data[i + 2];
    out += kB64Chars[(b >> 18) & 0x3F];
    out += kB64Chars[(b >> 12) & 0x3F];
    if (i + 1 < len) out += kB64Chars[(b >> 6) & 0x3F];
    if (i + 2 < len) out += kB64Chars[b & 0x3F];
  }
  return out;
}

// Generate a device key derived from the chip MAC + a fixed salt.
// This is deterministic (same on every boot) and unique per device.
// The key is a 32-byte value encoded as base64url (~43 chars).
static std::string brewos_derive_device_key() {
  uint64_t mac = ESP.getEfuseMac();
  // Simple key derivation: XOR MAC bytes with a salt pattern, then
  // expand to 32 bytes by repeating with different rotations.
  uint8_t key_bytes[32];
  const uint8_t salt[] = {0xBE, 0xEF, 0xCA, 0xFE, 0xDE, 0xAD, 0xC0, 0xDE};
  for (int i = 0; i < 32; i++) {
    uint8_t mac_byte = (mac >> ((i % 8) * 8)) & 0xFF;
    key_bytes[i] = mac_byte ^ salt[i % 8] ^ static_cast<uint8_t>(i * 7);
  }
  return base64url_encode(key_bytes, sizeof(key_bytes));
}

// Parse <url> (https://host[:port] or http://host[:port]) and open the
// WebSocket connection to /ws/device?id=<id>&key=<key>.
static void brewos_connect_ws(const std::string &base_url,
                               const std::string &device_id,
                               const std::string &device_key) {
  std::string path = "/ws/device?id=" + device_id + "&key=" + device_key;

  bool use_tls = false;
  std::string rest = base_url;
  uint16_t port = 80;

  if (rest.size() >= 8 && rest.substr(0, 8) == "https://") {
    use_tls = true;
    port = 443;
    rest = rest.substr(8);
  } else if (rest.size() >= 7 && rest.substr(0, 7) == "http://") {
    rest = rest.substr(7);
  }

  // Strip any trailing path from the host component.
  size_t slash = rest.find('/');
  std::string host_port = (slash != std::string::npos) ? rest.substr(0, slash) : rest;

  size_t colon = host_port.find(':');
  std::string host;
  if (colon != std::string::npos) {
    host = host_port.substr(0, colon);
    port = static_cast<uint16_t>(std::stoul(host_port.substr(colon + 1)));
  } else {
    host = host_port;
  }

  if (use_tls) {
    g_ws_client.beginSSL(host.c_str(), port, path.c_str());
  } else {
    g_ws_client.begin(host.c_str(), port, path.c_str());
  }
  g_ws_client.onEvent(ws_event);
  g_ws_client.setReconnectInterval(5000);
}
#endif  // USE_ESP32_FRAMEWORK_ARDUINO

// ---------------------------------------------------------------------------
// Minimal JSON helpers — no external dependencies, work in test builds.
// ---------------------------------------------------------------------------

// Extract the value of a string field: "key": "value"
static std::string json_extract_string(const std::string &json, const char *key) {
  std::string search = std::string("\"") + key + "\"";
  size_t pos = json.find(search);
  if (pos == std::string::npos) return "";
  pos += search.size();
  // Skip colon and any whitespace, then find the opening quote.
  pos = json.find('"', pos);
  if (pos == std::string::npos) return "";
  size_t end = json.find('"', pos + 1);
  if (end == std::string::npos) return "";
  return json.substr(pos + 1, end - pos - 1);
}

// Extract the value of a numeric field: "key": <number>
static float json_extract_float(const std::string &json, const char *key,
                                 float default_val) {
  std::string search = std::string("\"") + key + "\"";
  size_t pos = json.find(search);
  if (pos == std::string::npos) return default_val;
  pos = json.find(':', pos + search.size());
  if (pos == std::string::npos) return default_val;
  float val = default_val;
  sscanf(json.c_str() + pos + 1, " %f", &val);
  return val;
}

namespace esphome {
namespace espresso_machine_brewos {

static const char *const TAG = "brewos";

// ---------------------------------------------------------------------------
// setup() — open the WebSocket connection (Arduino builds only).
// ---------------------------------------------------------------------------
void BrewOSConnector::setup() {
#ifdef USE_ESP32_FRAMEWORK_ARDUINO
  g_connector_ptr = this;
  std::string device_id = brewos_get_device_id();
  std::string device_key = brewos_derive_device_key();
  ESP_LOGI(TAG, "BrewOS device ID : %s", device_id.c_str());
  ESP_LOGI(TAG, "BrewOS cloud URL : %s", url_.c_str());
  brewos_connect_ws(url_, device_id, device_key);
#endif
}

// ---------------------------------------------------------------------------
// loop() — drive the WebSocket and send periodic status updates.
//
// SAFETY: When the machine is busy (brewing, steaming, flushing), we skip
// calling g_ws_client.loop() because it can block for up to
// WEBSOCKETS_TCP_TIMEOUT ms during reconnection attempts. This ensures the
// PID and orchestrator state machines are never starved.
//
// Trade-off: Cloud commands are not processed during active operations.
// This is acceptable because:
//   1. The machine should not accept conflicting commands mid-brew anyway.
//   2. Local (HA API) commands still work.
//   3. Status updates resume immediately after the operation completes.
// ---------------------------------------------------------------------------
void BrewOSConnector::loop() {
#ifdef USE_ESP32_FRAMEWORK_ARDUINO
  // Skip potentially-blocking WS handling during critical operations.
  bool machine_busy = (machine_ != nullptr) && machine_->is_busy();
  if (!machine_busy) {
    g_ws_client.loop();
  }
  connected_ = g_ws_connected_flag;

  uint32_t now = millis();
  if (connected_ && (now - last_status_ms_) >= STATUS_INTERVAL_MS) {
    last_status_ms_ = now;
    send_raw(build_status_json());
  }
#endif
}

// ---------------------------------------------------------------------------
// on_message() — dispatch a command received from the BrewOS cloud.
// ---------------------------------------------------------------------------
void BrewOSConnector::on_message(const std::string &json) {
  if (machine_ == nullptr) return;

  std::string type = json_extract_string(json, "type");
  if (type.empty()) {
    ESP_LOGW(TAG, "BrewOS: message missing 'type' field: %.80s", json.c_str());
    return;
  }

  ESP_LOGD(TAG, "BrewOS command received: %s", type.c_str());

  if (type == "brew_start") {
    machine_->brew_start();
  } else if (type == "brew_stop") {
    machine_->brew_stop();
  } else if (type == "steam_start") {
    machine_->steam_start();
  } else if (type == "steam_stop") {
    machine_->steam_stop();
  } else if (type == "machine_on") {
    machine_->machine_on();
  } else if (type == "machine_off") {
    machine_->machine_off();
  } else if (type == "flush") {
    float volume_ml = json_extract_float(json, "volume_ml", 0.0f);
    if (volume_ml > 0.0f) {
      machine_->flush(volume_ml);
    } else {
      ESP_LOGW(TAG, "BrewOS flush: missing or zero volume_ml — ignored");
    }
  } else {
    ESP_LOGD(TAG, "BrewOS: unknown command '%s' — ignored", type.c_str());
  }
}

// ---------------------------------------------------------------------------
// build_status_json() — serialise machine state into pico_status JSON.
// ---------------------------------------------------------------------------
std::string BrewOSConnector::build_status_json() const {
  const char *mode = "unknown";
  int brew_state = 0;
  int steam_state = 0;
  bool is_busy = false;
  bool powered_on = false;

  if (machine_ != nullptr) {
    mode = machine_->mode_name();
    brew_state = static_cast<int>(machine_->get_brew_state());
    steam_state = static_cast<int>(machine_->get_steam_state());
    is_busy = machine_->is_busy();
    powered_on = machine_->is_powered_on();
  }

  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"type\":\"pico_status\",\"mode\":\"%s\","
           "\"brew_state\":%d,\"steam_state\":%d,"
           "\"is_busy\":%s,\"powered_on\":%s}",
           mode, brew_state, steam_state, is_busy ? "true" : "false",
           powered_on ? "true" : "false");
  return std::string(buf);
}

// ---------------------------------------------------------------------------
// send_raw() — write a JSON string over the WebSocket.
// ---------------------------------------------------------------------------
void BrewOSConnector::send_raw(const std::string &json) {
#ifdef USE_ESP32_FRAMEWORK_ARDUINO
  if (g_ws_connected_flag) {
    g_ws_client.sendTXT(json.c_str(), json.size());
  }
#else
  (void)json;  // suppress unused-parameter warning in test builds
#endif
}

}  // namespace espresso_machine_brewos
}  // namespace esphome
