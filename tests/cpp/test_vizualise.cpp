// Unit tests for the Vizualise shot upload component.
//
// All HTTP transport is mocked — no real network calls are made.  The tests
// exercise shot data collection, JSON serialisation (visualizer.coffee
// parallel-arrays format), the upload retry / success / failure paths, and
// auto-recording when wired to the EspressoMachine orchestrator.

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "components/espresso_machine_vizualise/vizualise.h"
#include "components/espresso_machine/espresso_machine.h"
#include "components/espresso_machine/interfaces.h"

// Pull in the mock millis() state.
extern uint32_t g_mock_millis;

using esphome::espresso_machine_vizualise::VizualiseShotUpload;
using namespace esphome::espresso_machine;

// ---------------------------------------------------------------------------
// MockVizualiseUpload — captures http_post calls for verification
// ---------------------------------------------------------------------------
class MockVizualiseUpload : public VizualiseShotUpload {
 public:
  int mock_http_status{200};
  std::string last_url;
  std::string last_auth;
  std::string last_body;
  int post_call_count{0};

  int http_post(const std::string &url, const std::string &auth_header,
                const std::string &body) override {
    last_url = url;
    last_auth = auth_header;
    last_body = body;
    post_call_count++;
    return mock_http_status;
  }
};

// ---------------------------------------------------------------------------
// Minimal mock hardware for orchestrator integration tests.
// Wrapped in an anonymous namespace to avoid ODR collisions.
// ---------------------------------------------------------------------------
namespace {

struct MockValveViz : public IValve {
  bool open_state = false;
  void open() override { open_state = true; }
  void close() override { open_state = false; }
  bool is_open() const override { return open_state; }
};

struct MockPumpViz : public IPump {
  bool running = false;
  float volume = 0.0f;
  float rate = 0.0f;

  void turn_on() override { running = true; }
  void turn_off() override { running = false; }
  bool is_running() const override { return running; }
  float get_flow_rate() const override { return rate; }
  float get_flow_total() const override { return volume; }
  void reset_flow() override { volume = 0.0f; rate = 0.0f; }
};

struct MockHeaterViz : public IHeater {
  float temp = 25.0f;
  float target = 90.0f;

  float get_current_temperature() const override { return temp; }
  void set_target_temperature(float t) override { target = t; }
  bool is_ready(float target_temp) const override { return temp >= target_temp; }
};

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Helper: create a pre-configured MockVizualiseUpload.
// ---------------------------------------------------------------------------
static MockVizualiseUpload make_uploader(
    const std::string &server = "https://test.visualizer.coffee",
    const std::string &token = "test_token_abc",
    const std::string &profile = "Test Profile",
    const std::string &machine = "Test Machine") {
  MockVizualiseUpload u;
  u.set_server_url(server);
  u.set_api_token(token);
  u.set_profile_name(profile);
  u.set_machine_name(machine);
  return u;
}

// ===========================================================================
// Configuration
// ===========================================================================

TEST(VizualiseConfig, DefaultServerUrl) {
  VizualiseShotUpload u;
  EXPECT_EQ(u.get_server_url(), "https://visualizer.coffee");
}

TEST(VizualiseConfig, CustomServerUrl) {
  VizualiseShotUpload u;
  u.set_server_url("https://custom.server.io");
  EXPECT_EQ(u.get_server_url(), "https://custom.server.io");
}

TEST(VizualiseConfig, ApiTokenStored) {
  VizualiseShotUpload u;
  u.set_api_token("my_secret_token");
  EXPECT_EQ(u.get_api_token(), "my_secret_token");
}

TEST(VizualiseConfig, ProfileNameDefault) {
  VizualiseShotUpload u;
  EXPECT_EQ(u.get_profile_name(), "Manual");
}

TEST(VizualiseConfig, ProfileNameCustom) {
  VizualiseShotUpload u;
  u.set_profile_name("Lever-Style Decline");
  EXPECT_EQ(u.get_profile_name(), "Lever-Style Decline");
}

TEST(VizualiseConfig, MachineNameDefault) {
  VizualiseShotUpload u;
  EXPECT_EQ(u.get_machine_name(), "ESPHome Espresso Machine");
}

TEST(VizualiseConfig, MachineNameCustom) {
  VizualiseShotUpload u;
  u.set_machine_name("My Gaggia Classic");
  EXPECT_EQ(u.get_machine_name(), "My Gaggia Classic");
}

TEST(VizualiseConfig, TrailingSlashNormalized) {
  VizualiseShotUpload u;
  u.set_server_url("https://example.io/");
  EXPECT_EQ(u.get_server_url(), "https://example.io");
}

TEST(VizualiseConfig, MultipleTrailingSlashesNormalized) {
  VizualiseShotUpload u;
  u.set_server_url("https://example.io///");
  EXPECT_EQ(u.get_server_url(), "https://example.io");
}

// ===========================================================================
// Shot recording
// ===========================================================================

TEST(VizualiseRecording, InitialStateNotRecording) {
  VizualiseShotUpload u;
  EXPECT_FALSE(u.is_recording());
  EXPECT_FALSE(u.has_pending_upload());
  EXPECT_EQ(u.get_shot_id(), 0u);
  EXPECT_EQ(u.get_datapoint_count(), 0u);
}

TEST(VizualiseRecording, BeginShotStartsRecording) {
  VizualiseShotUpload u;
  g_mock_millis = 5000;
  u.begin_shot();
  EXPECT_TRUE(u.is_recording());
  EXPECT_FALSE(u.has_pending_upload());
  EXPECT_EQ(u.get_shot_id(), 1u);
}

TEST(VizualiseRecording, ShotIdIncrements) {
  VizualiseShotUpload u;
  g_mock_millis = 1000;
  u.begin_shot();
  u.end_shot(5000);
  EXPECT_EQ(u.get_shot_id(), 1u);

  u.begin_shot();
  EXPECT_EQ(u.get_shot_id(), 2u);
  u.end_shot(6000);

  u.begin_shot();
  EXPECT_EQ(u.get_shot_id(), 3u);
}

TEST(VizualiseRecording, DatapointAccumulation) {
  VizualiseShotUpload u;
  g_mock_millis = 0;
  u.begin_shot();

  u.add_datapoint(0.0f, 0.1f, 92.5f, 7.0f, 0.0f);
  u.add_datapoint(100.0f, 0.2f, 92.7f, 6.8f, 0.5f);
  u.add_datapoint(200.0f, 0.5f, 92.9f, 5.2f, 1.2f);

  EXPECT_EQ(u.get_datapoint_count(), 3u);
}

TEST(VizualiseRecording, DatapointIgnoredWhenNotRecording) {
  VizualiseShotUpload u;
  u.add_datapoint(0.0f, 0.1f, 92.5f, 7.0f, 0.0f);
  EXPECT_EQ(u.get_datapoint_count(), 0u);
}

TEST(VizualiseRecording, EndShotSetsPending) {
  VizualiseShotUpload u;
  g_mock_millis = 0;
  u.begin_shot();
  u.add_datapoint(0.0f, 9.0f, 93.0f, 2.5f, 0.0f);
  u.end_shot(25000);

  EXPECT_FALSE(u.is_recording());
  EXPECT_TRUE(u.has_pending_upload());
  EXPECT_EQ(u.get_shot_duration_ms(), 25000u);
}

TEST(VizualiseRecording, EndShotIgnoredWhenNotRecording) {
  VizualiseShotUpload u;
  u.end_shot(5000);
  EXPECT_FALSE(u.has_pending_upload());
}

TEST(VizualiseRecording, BeginShotClearsPreviousData) {
  VizualiseShotUpload u;
  g_mock_millis = 0;
  u.begin_shot();
  u.add_datapoint(0.0f, 1.0f, 90.0f, 3.0f, 0.0f);
  u.end_shot(10000);
  EXPECT_EQ(u.get_datapoint_count(), 1u);

  u.begin_shot();
  EXPECT_EQ(u.get_datapoint_count(), 0u);
  EXPECT_FALSE(u.has_pending_upload());
}

// ===========================================================================
// JSON serialisation — parallel-arrays format
// ===========================================================================

TEST(VizualiseJson, EmptyDatapointsReturnsEmptyString) {
  VizualiseShotUpload u;
  EXPECT_TRUE(u.serialize_shot_json().empty());
}

TEST(VizualiseJson, BasicStructure) {
  auto u = make_uploader();
  g_mock_millis = 1000000;
  u.begin_shot();
  u.add_datapoint(0.0f, 0.1f, 92.5f, 7.0f, 0.0f);
  u.end_shot(25000);

  std::string json = u.serialize_shot_json();

  // Top-level keys
  EXPECT_NE(json.find("\"start_time\":"), std::string::npos);
  EXPECT_NE(json.find("\"machine\":\"Test Machine\""), std::string::npos);
  EXPECT_NE(json.find("\"profile\":{\"name\":\"Test Profile\"}"), std::string::npos);
  EXPECT_NE(json.find("\"sample_interval\":100"), std::string::npos);
  EXPECT_NE(json.find("\"data\":{"), std::string::npos);

  // Parallel array keys
  EXPECT_NE(json.find("\"time\":["), std::string::npos);
  EXPECT_NE(json.find("\"pressure\":["), std::string::npos);
  EXPECT_NE(json.find("\"temperature\":["), std::string::npos);
  EXPECT_NE(json.find("\"flow\":["), std::string::npos);
  EXPECT_NE(json.find("\"weight\":["), std::string::npos);
}

TEST(VizualiseJson, TimestampInSeconds) {
  auto u = make_uploader();
  // millis() → 2000 ms = 2 s timestamp
  g_mock_millis = 2000;
  u.begin_shot();
  u.add_datapoint(0.0f, 0.0f, 90.0f, 0.0f, 0.0f);
  u.end_shot(1000);

  std::string json = u.serialize_shot_json();
  EXPECT_NE(json.find("\"start_time\":2"), std::string::npos);
}

TEST(VizualiseJson, ParallelArrayValues) {
  auto u = make_uploader();
  g_mock_millis = 0;
  u.begin_shot();
  u.add_datapoint(100.0f, 2.30f, 93.50f, 4.20f, 1.50f);
  u.end_shot(5000);

  std::string json = u.serialize_shot_json();

  EXPECT_NE(json.find("\"time\":[100.00]"), std::string::npos);
  EXPECT_NE(json.find("\"pressure\":[2.30]"), std::string::npos);
  EXPECT_NE(json.find("\"temperature\":[93.50]"), std::string::npos);
  EXPECT_NE(json.find("\"flow\":[4.20]"), std::string::npos);
  EXPECT_NE(json.find("\"weight\":[1.50]"), std::string::npos);
}

TEST(VizualiseJson, MultipleDatapoints) {
  auto u = make_uploader();
  g_mock_millis = 0;
  u.begin_shot();
  u.add_datapoint(0.0f, 0.0f, 92.0f, 0.0f, 0.0f);
  u.add_datapoint(100.0f, 1.0f, 92.5f, 3.0f, 0.5f);
  u.add_datapoint(200.0f, 2.0f, 93.0f, 5.0f, 1.0f);
  u.end_shot(200);

  std::string json = u.serialize_shot_json();

  // Three values in each array → two commas per array.
  // Check time array has three entries.
  size_t count = 0;
  size_t pos = json.find("\"time\":[");
  if (pos != std::string::npos) {
    size_t end = json.find("]", pos);
    std::string arr = json.substr(pos, end - pos);
    for (char c : arr)
      if (c == ',')
        count++;
  }
  EXPECT_EQ(count, 2u);
}

TEST(VizualiseJson, MachineNameWithQuotesEscaped) {
  VizualiseShotUpload u;
  u.set_machine_name("My \"Best\" Machine");
  g_mock_millis = 0;
  u.begin_shot();
  u.add_datapoint(0.0f, 0.0f, 90.0f, 0.0f, 0.0f);
  u.end_shot(1000);

  std::string json = u.serialize_shot_json();
  EXPECT_NE(json.find("My \\\"Best\\\" Machine"), std::string::npos);
}

TEST(VizualiseJson, ProfileNameWithQuotesEscaped) {
  VizualiseShotUpload u;
  u.set_profile_name("He said \"hello\"");
  g_mock_millis = 0;
  u.begin_shot();
  u.add_datapoint(0.0f, 0.0f, 90.0f, 0.0f, 0.0f);
  u.end_shot(1000);

  std::string json = u.serialize_shot_json();
  EXPECT_NE(json.find("He said \\\"hello\\\""), std::string::npos);
}

// ===========================================================================
// Upload
// ===========================================================================

TEST(VizualiseUpload, SuccessfulUploadClearsPending) {
  auto u = make_uploader();
  u.mock_http_status = 201;
  g_mock_millis = 0;

  u.begin_shot();
  u.add_datapoint(0.0f, 9.0f, 93.0f, 2.5f, 0.0f);
  u.end_shot(25000);

  EXPECT_TRUE(u.has_pending_upload());
  bool ok = u.upload_pending_shot();
  EXPECT_TRUE(ok);
  EXPECT_FALSE(u.has_pending_upload());
}

TEST(VizualiseUpload, FailedUploadKeepsPending) {
  auto u = make_uploader();
  u.mock_http_status = 500;
  g_mock_millis = 0;

  u.begin_shot();
  u.add_datapoint(0.0f, 9.0f, 93.0f, 2.5f, 0.0f);
  u.end_shot(25000);

  bool ok = u.upload_pending_shot();
  EXPECT_FALSE(ok);
  EXPECT_TRUE(u.has_pending_upload());
}

TEST(VizualiseUpload, NoPendingShotReturns) {
  auto u = make_uploader();
  EXPECT_FALSE(u.upload_pending_shot());
  EXPECT_EQ(u.post_call_count, 0);
}

TEST(VizualiseUpload, CorrectUrlConstructed) {
  auto u = make_uploader("https://my.server.io", "tok123", "Profile", "Machine");
  g_mock_millis = 0;

  u.begin_shot();
  u.add_datapoint(0.0f, 0.0f, 90.0f, 0.0f, 0.0f);
  u.end_shot(1000);
  u.upload_pending_shot();

  EXPECT_EQ(u.last_url, "https://my.server.io/api/shots/upload");
}

TEST(VizualiseUpload, TrailingSlashUrlNormalized) {
  auto u = make_uploader("https://test.io/", "tok", "P", "M");
  g_mock_millis = 0;

  u.begin_shot();
  u.add_datapoint(0.0f, 0.0f, 90.0f, 0.0f, 0.0f);
  u.end_shot(1000);
  u.upload_pending_shot();

  EXPECT_EQ(u.last_url, "https://test.io/api/shots/upload");
}

TEST(VizualiseUpload, BearerTokenInAuthHeader) {
  auto u = make_uploader("https://s.io", "secret_token_xyz", "P", "M");
  g_mock_millis = 0;

  u.begin_shot();
  u.add_datapoint(0.0f, 0.0f, 90.0f, 0.0f, 0.0f);
  u.end_shot(1000);
  u.upload_pending_shot();

  EXPECT_EQ(u.last_auth, "Bearer secret_token_xyz");
}

TEST(VizualiseUpload, BodyContainsSerializedJson) {
  auto u = make_uploader();
  g_mock_millis = 0;

  u.begin_shot();
  u.add_datapoint(0.0f, 9.0f, 93.0f, 2.5f, 0.0f);
  u.end_shot(25000);
  u.upload_pending_shot();

  EXPECT_FALSE(u.last_body.empty());
  EXPECT_NE(u.last_body.find("\"data\""), std::string::npos);
  EXPECT_NE(u.last_body.find("\"time\""), std::string::npos);
}

TEST(VizualiseUpload, TransportErrorKeepsPending) {
  auto u = make_uploader();
  u.mock_http_status = -1;
  g_mock_millis = 0;

  u.begin_shot();
  u.add_datapoint(0.0f, 0.0f, 90.0f, 0.0f, 0.0f);
  u.end_shot(1000);

  bool ok = u.upload_pending_shot();
  EXPECT_FALSE(ok);
  EXPECT_TRUE(u.has_pending_upload());
}

TEST(VizualiseUpload, RetrySucceedsOnSecondAttempt) {
  auto u = make_uploader();
  g_mock_millis = 0;

  u.begin_shot();
  u.add_datapoint(0.0f, 0.0f, 90.0f, 0.0f, 0.0f);
  u.end_shot(1000);

  u.mock_http_status = 503;
  EXPECT_FALSE(u.upload_pending_shot());
  EXPECT_TRUE(u.has_pending_upload());

  u.mock_http_status = 200;
  EXPECT_TRUE(u.upload_pending_shot());
  EXPECT_FALSE(u.has_pending_upload());
}

TEST(VizualiseUpload, Http200And201BothSucceed) {
  for (int code : {200, 201}) {
    auto u = make_uploader();
    u.mock_http_status = code;
    g_mock_millis = 0;

    u.begin_shot();
    u.add_datapoint(0.0f, 0.0f, 90.0f, 0.0f, 0.0f);
    u.end_shot(1000);

    EXPECT_TRUE(u.upload_pending_shot()) << "HTTP " << code << " should succeed";
  }
}

TEST(VizualiseUpload, Http4xxFailsUpload) {
  for (int code : {400, 401, 403, 404}) {
    auto u = make_uploader();
    u.mock_http_status = code;
    g_mock_millis = 0;

    u.begin_shot();
    u.add_datapoint(0.0f, 0.0f, 90.0f, 0.0f, 0.0f);
    u.end_shot(1000);

    EXPECT_FALSE(u.upload_pending_shot()) << "HTTP " << code << " should fail";
    EXPECT_TRUE(u.has_pending_upload());
  }
}

// ===========================================================================
// Loop integration (manual — no orchestrator)
// ===========================================================================

TEST(VizualiseLoop, LoopUploadsWhenPending) {
  auto u = make_uploader();
  u.mock_http_status = 200;
  g_mock_millis = 0;

  u.begin_shot();
  u.add_datapoint(0.0f, 0.0f, 90.0f, 0.0f, 0.0f);
  u.end_shot(1000);

  EXPECT_TRUE(u.has_pending_upload());
  u.loop();
  EXPECT_FALSE(u.has_pending_upload());
  EXPECT_EQ(u.post_call_count, 1);
}

TEST(VizualiseLoop, LoopDoesNothingWhenNoPending) {
  auto u = make_uploader();
  u.loop();
  EXPECT_EQ(u.post_call_count, 0);
}

TEST(VizualiseLoop, RetryBackoffPreventsImmediateRetry) {
  auto u = make_uploader();
  u.mock_http_status = 500;
  g_mock_millis = 1000;

  u.begin_shot();
  u.add_datapoint(0.0f, 0.0f, 90.0f, 0.0f, 0.0f);
  u.end_shot(1000);

  u.loop();  // first attempt — fails
  EXPECT_EQ(u.post_call_count, 1);
  EXPECT_TRUE(u.has_pending_upload());

  // Rapid loop ticks should NOT trigger another HTTP request.
  g_mock_millis += 10;
  u.loop();
  g_mock_millis += 10;
  u.loop();
  EXPECT_EQ(u.post_call_count, 1);

  // After the backoff interval elapses, retry should fire.
  g_mock_millis += VizualiseShotUpload::UPLOAD_INITIAL_RETRY_MS;
  u.loop();
  EXPECT_EQ(u.post_call_count, 2);
}

TEST(VizualiseLoop, RetryAbandonedAfterMaxRetries) {
  auto u = make_uploader();
  u.mock_http_status = 500;
  g_mock_millis = 0;

  u.begin_shot();
  u.add_datapoint(0.0f, 0.0f, 90.0f, 0.0f, 0.0f);
  u.end_shot(1000);

  for (int i = 0; i < VizualiseShotUpload::UPLOAD_MAX_RETRIES + 1; i++) {
    g_mock_millis += 600000;
    u.loop();
  }

  EXPECT_FALSE(u.has_pending_upload());
}

// ===========================================================================
// Edge cases
// ===========================================================================

TEST(VizualiseEdge, LargeShotSerializes) {
  auto u = make_uploader();
  g_mock_millis = 0;
  u.begin_shot();

  for (int i = 0; i < 300; i++) {
    float t = i * 100.0f;
    u.add_datapoint(t, 9.0f, 93.0f, 2.5f, t * 0.012f);
  }
  u.end_shot(30000);

  std::string json = u.serialize_shot_json();
  EXPECT_FALSE(json.empty());
  EXPECT_EQ(u.get_datapoint_count(), 300u);
}

TEST(VizualiseEdge, EmptyApiTokenStillUploads) {
  MockVizualiseUpload u;
  u.set_server_url("https://test.io");
  u.set_api_token("");
  u.mock_http_status = 401;
  g_mock_millis = 0;

  u.begin_shot();
  u.add_datapoint(0.0f, 0.0f, 90.0f, 0.0f, 0.0f);
  u.end_shot(1000);
  u.upload_pending_shot();

  EXPECT_EQ(u.last_auth, "Bearer ");
  EXPECT_EQ(u.post_call_count, 1);
}

// ===========================================================================
// Auto-recording — vizualise wired to EspressoMachine orchestrator
// ===========================================================================

// Helper: set up a minimal orchestrator with mock hardware.
struct VizualiseOrchestratorFixture {
  MockValveViz brew_valve;
  MockValveViz purge_valve;
  MockValveViz steam_valve;
  MockValveViz steam_purge_valve;
  MockPumpViz brew_pump;
  MockPumpViz steam_pump;
  MockHeaterViz heater;
  EspressoMachine machine;
  MockVizualiseUpload uploader;

  VizualiseOrchestratorFixture() {
    machine.set_brew_valve(&brew_valve);
    machine.set_brew_purge_valve(&purge_valve);
    machine.set_brew_pump(&brew_pump);
    machine.set_brew_heater_ctrl(&heater);
    machine.set_brew_target_temperature(90.0f);
    machine.set_brew_flow_max(40.0f);
    machine.set_brew_flow_offset(20.0f);

    machine.set_steam_valve(&steam_valve);
    machine.set_steam_purge_valve(&steam_purge_valve);
    machine.set_steam_pump(&steam_pump);
    machine.set_steam_target_temperature(135.0f);
    machine.set_steam_flow_max(2.0f);
    machine.set_steam_cool_down_to(90.0f);

    uploader.set_server_url("https://test.visualizer.coffee");
    uploader.set_api_token("test_token");
    uploader.set_profile_name("Test");
    uploader.set_machine_name("Test Machine");
    uploader.set_machine(&machine);
    uploader.mock_http_status = 200;

    g_mock_millis = 0;
    machine.setup();
    machine.machine_on();
  }
};

TEST(VizualiseAutoRecord, StartsRecordingOnBrewStart) {
  VizualiseOrchestratorFixture f;

  f.heater.temp = 90.0f;
  f.machine.brew_start();
  f.machine.loop();

  f.uploader.loop();

  EXPECT_TRUE(f.uploader.is_recording());
  EXPECT_EQ(f.uploader.get_shot_id(), 1u);
}

TEST(VizualiseAutoRecord, SamplesDatapointsDuringBrew) {
  VizualiseOrchestratorFixture f;
  f.heater.temp = 90.0f;
  f.machine.brew_start();
  f.machine.loop();
  f.uploader.loop();  // Rising edge detected, recording starts.

  for (int i = 1; i <= 5; i++) {
    g_mock_millis = i * 100;
    f.brew_pump.rate = 2.5f;
    f.machine.loop();
    f.uploader.loop();
  }

  EXPECT_GE(f.uploader.get_datapoint_count(), 5u);
}

TEST(VizualiseAutoRecord, EndsRecordingOnBrewStop) {
  VizualiseOrchestratorFixture f;
  f.heater.temp = 90.0f;
  f.machine.brew_start();
  f.machine.loop();
  f.uploader.loop();  // Recording starts.

  g_mock_millis = 100;
  f.machine.loop();
  f.uploader.loop();

  f.machine.brew_stop();
  g_mock_millis = 200;
  f.machine.loop();
  f.uploader.loop();  // Falling edge → end_shot() + upload in same tick.

  EXPECT_FALSE(f.uploader.is_recording());
  EXPECT_EQ(f.uploader.post_call_count, 1);
  EXPECT_FALSE(f.uploader.has_pending_upload());
}

TEST(VizualiseAutoRecord, NoRecordingWithoutMachine) {
  MockVizualiseUpload u;
  u.set_server_url("https://test.io");
  u.set_api_token("tok");

  g_mock_millis = 0;
  u.loop();
  g_mock_millis = 100;
  u.loop();

  EXPECT_FALSE(u.is_recording());
  EXPECT_EQ(u.get_shot_id(), 0u);
}

TEST(VizualiseAutoRecord, NoRecordingDuringSteam) {
  VizualiseOrchestratorFixture f;
  f.heater.temp = 135.0f;

  f.machine.steam_start();
  f.machine.loop();
  f.uploader.loop();

  EXPECT_FALSE(f.uploader.is_recording());
  EXPECT_EQ(f.uploader.get_shot_id(), 0u);
}

TEST(VizualiseAutoRecord, SecondBrewCreatesNewShot) {
  VizualiseOrchestratorFixture f;
  f.heater.temp = 90.0f;

  // First brew.
  f.machine.brew_start();
  f.machine.loop();
  f.uploader.loop();
  g_mock_millis = 100;
  f.machine.loop();
  f.uploader.loop();
  f.machine.brew_stop();
  g_mock_millis = 200;
  f.machine.loop();
  f.uploader.loop();
  EXPECT_EQ(f.uploader.get_shot_id(), 1u);

  g_mock_millis = 300;
  f.uploader.loop();
  EXPECT_EQ(f.uploader.post_call_count, 1);

  // Second brew.
  g_mock_millis = 1000;
  f.machine.brew_start();
  f.machine.loop();
  f.uploader.loop();
  EXPECT_EQ(f.uploader.get_shot_id(), 2u);
  EXPECT_TRUE(f.uploader.is_recording());
}
