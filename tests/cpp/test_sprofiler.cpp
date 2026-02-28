// Unit tests for the Sprofiler shot upload component.
//
// All HTTP transport is mocked — no real network calls are made.  The tests
// exercise shot data collection, JSON serialisation (Gaggiuino format), the
// upload retry / success / failure paths, and auto-recording when wired to
// the EspressoMachine orchestrator.

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "components/espresso_machine_sprofiler/sprofiler.h"
#include "components/espresso_machine/espresso_machine.h"
#include "components/espresso_machine/interfaces.h"

// Pull in the mock millis() state.
extern uint32_t g_mock_millis;

using esphome::espresso_machine_sprofiler::ShotDatapoint;
using esphome::espresso_machine_sprofiler::SprofilerShotUpload;
using namespace esphome::espresso_machine;

// ---------------------------------------------------------------------------
// MockSprofilerUpload — captures http_post calls for verification
// ---------------------------------------------------------------------------
class MockSprofilerUpload : public SprofilerShotUpload {
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
// Wrapped in an anonymous namespace to avoid ODR collisions with the mocks
// in test_orchestrator.cpp (which have the same names but different layouts).
// ---------------------------------------------------------------------------
namespace {

struct MockValve : public IValve {
  bool open_state = false;
  void open() override { open_state = true; }
  void close() override { open_state = false; }
  bool is_open() const override { return open_state; }
};

struct MockPump : public IPump {
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

struct MockHeater : public IHeater {
  float temp = 25.0f;
  float target = 90.0f;

  float get_current_temperature() const override { return temp; }
  void set_target_temperature(float t) override { target = t; }
  bool is_ready(float target_temp) const override { return temp >= target_temp; }
};

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Helper: create a pre-configured MockSprofilerUpload.
// ---------------------------------------------------------------------------
static MockSprofilerUpload make_uploader(
    const std::string &server = "https://test.sprofiler.io",
    const std::string &token = "test_token_abc",
    const std::string &profile = "Test Profile") {
  MockSprofilerUpload u;
  u.set_server_url(server);
  u.set_api_token(token);
  u.set_profile_name(profile);
  return u;
}

// ===========================================================================
// Configuration
// ===========================================================================

TEST(SprofilerConfig, DefaultServerUrl) {
  SprofilerShotUpload u;
  EXPECT_EQ(u.get_server_url(), "https://sprofiler.io");
}

TEST(SprofilerConfig, CustomServerUrl) {
  SprofilerShotUpload u;
  u.set_server_url("https://custom.server.io");
  EXPECT_EQ(u.get_server_url(), "https://custom.server.io");
}

TEST(SprofilerConfig, ApiTokenStored) {
  SprofilerShotUpload u;
  u.set_api_token("my_secret_token");
  EXPECT_EQ(u.get_api_token(), "my_secret_token");
}

TEST(SprofilerConfig, ProfileNameDefault) {
  SprofilerShotUpload u;
  EXPECT_EQ(u.get_profile_name(), "Manual");
}

TEST(SprofilerConfig, ProfileNameCustom) {
  SprofilerShotUpload u;
  u.set_profile_name("Lever-Style Decline");
  EXPECT_EQ(u.get_profile_name(), "Lever-Style Decline");
}

TEST(SprofilerConfig, TrailingSlashNormalized) {
  SprofilerShotUpload u;
  u.set_server_url("https://example.io/");
  EXPECT_EQ(u.get_server_url(), "https://example.io");
}

TEST(SprofilerConfig, MultipleTrailingSlashesNormalized) {
  SprofilerShotUpload u;
  u.set_server_url("https://example.io///");
  EXPECT_EQ(u.get_server_url(), "https://example.io");
}

// ===========================================================================
// Shot recording
// ===========================================================================

TEST(SprofilerRecording, InitialStateNotRecording) {
  SprofilerShotUpload u;
  EXPECT_FALSE(u.is_recording());
  EXPECT_FALSE(u.has_pending_upload());
  EXPECT_EQ(u.get_shot_id(), 0u);
}

TEST(SprofilerRecording, BeginShotStartsRecording) {
  SprofilerShotUpload u;
  g_mock_millis = 5000;
  u.begin_shot();
  EXPECT_TRUE(u.is_recording());
  EXPECT_FALSE(u.has_pending_upload());
  EXPECT_EQ(u.get_shot_id(), 1u);
}

TEST(SprofilerRecording, ShotIdIncrements) {
  SprofilerShotUpload u;
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

TEST(SprofilerRecording, DatapointAccumulation) {
  SprofilerShotUpload u;
  g_mock_millis = 0;
  u.begin_shot();

  u.add_datapoint(0.0f, 0.1f, 92.5f, 7.0f, 0.0f);
  u.add_datapoint(0.1f, 0.2f, 92.7f, 6.8f, 0.5f);
  u.add_datapoint(0.2f, 0.5f, 92.9f, 5.2f, 1.2f);

  EXPECT_EQ(u.get_datapoints().size(), 3u);
  EXPECT_FLOAT_EQ(u.get_datapoints()[0].time_sec, 0.0f);
  EXPECT_FLOAT_EQ(u.get_datapoints()[1].pressure_bar, 0.2f);
  EXPECT_FLOAT_EQ(u.get_datapoints()[2].weight_g, 1.2f);
}

TEST(SprofilerRecording, DatapointIgnoredWhenNotRecording) {
  SprofilerShotUpload u;
  u.add_datapoint(0.0f, 0.1f, 92.5f, 7.0f, 0.0f);
  EXPECT_TRUE(u.get_datapoints().empty());
}

TEST(SprofilerRecording, EndShotSetsPending) {
  SprofilerShotUpload u;
  g_mock_millis = 0;
  u.begin_shot();
  u.add_datapoint(0.0f, 9.0f, 93.0f, 2.5f, 0.0f);
  u.end_shot(25000);

  EXPECT_FALSE(u.is_recording());
  EXPECT_TRUE(u.has_pending_upload());
  EXPECT_EQ(u.get_shot_duration_ms(), 25000u);
}

TEST(SprofilerRecording, EndShotIgnoredWhenNotRecording) {
  SprofilerShotUpload u;
  u.end_shot(5000);
  EXPECT_FALSE(u.has_pending_upload());
}

TEST(SprofilerRecording, BeginShotClearsPreviousData) {
  SprofilerShotUpload u;
  g_mock_millis = 0;
  u.begin_shot();
  u.add_datapoint(0.0f, 1.0f, 90.0f, 3.0f, 0.0f);
  u.end_shot(10000);
  EXPECT_EQ(u.get_datapoints().size(), 1u);

  u.begin_shot();
  EXPECT_TRUE(u.get_datapoints().empty());
  EXPECT_FALSE(u.has_pending_upload());
}

TEST(SprofilerRecording, BeginShotPreReservesCapacity) {
  SprofilerShotUpload u;
  g_mock_millis = 0;
  u.begin_shot();
  // Capacity should be pre-reserved so push_back doesn't reallocate during a shot.
  EXPECT_GE(u.get_datapoints().capacity(), 300u);
}

// ===========================================================================
// JSON serialisation
// ===========================================================================

TEST(SprofilerJson, EmptyDatapointsReturnsEmptyString) {
  SprofilerShotUpload u;
  EXPECT_TRUE(u.serialize_shot_json().empty());
}

TEST(SprofilerJson, BasicStructure) {
  auto u = make_uploader();
  g_mock_millis = 1000000;
  u.begin_shot();
  u.add_datapoint(0.0f, 0.1f, 92.5f, 7.0f, 0.0f);
  u.end_shot(25000);

  std::string json = u.serialize_shot_json();

  EXPECT_NE(json.find("\"id\":1"), std::string::npos);
  EXPECT_NE(json.find("\"timestamp\":1000"), std::string::npos);
  EXPECT_NE(json.find("\"duration\":25000"), std::string::npos);
  EXPECT_NE(json.find("\"profile\":{\"name\":\"Test Profile\"}"), std::string::npos);
  EXPECT_NE(json.find("\"datapoints\":["), std::string::npos);
}

TEST(SprofilerJson, DatapointValues) {
  auto u = make_uploader();
  g_mock_millis = 0;
  u.begin_shot();
  u.add_datapoint(0.10f, 2.30f, 93.50f, 4.20f, 1.50f);
  u.end_shot(5000);

  std::string json = u.serialize_shot_json();

  EXPECT_NE(json.find("\"time\":0.10"), std::string::npos);
  EXPECT_NE(json.find("\"pressure\":2.30"), std::string::npos);
  EXPECT_NE(json.find("\"temperature\":93.50"), std::string::npos);
  EXPECT_NE(json.find("\"flow\":4.20"), std::string::npos);
  EXPECT_NE(json.find("\"weight\":1.50"), std::string::npos);
}

TEST(SprofilerJson, MultipleDatapoints) {
  auto u = make_uploader();
  g_mock_millis = 0;
  u.begin_shot();
  u.add_datapoint(0.0f, 0.0f, 92.0f, 0.0f, 0.0f);
  u.add_datapoint(0.1f, 1.0f, 92.5f, 3.0f, 0.5f);
  u.add_datapoint(0.2f, 2.0f, 93.0f, 5.0f, 1.0f);
  u.end_shot(200);

  std::string json = u.serialize_shot_json();

  size_t count = 0;
  size_t pos = 0;
  while ((pos = json.find("\"time\":", pos)) != std::string::npos) {
    count++;
    pos++;
  }
  EXPECT_EQ(count, 3u);
}

TEST(SprofilerJson, ProfileNameWithQuotesEscaped) {
  SprofilerShotUpload u;
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

TEST(SprofilerUpload, SuccessfulUploadClearsPending) {
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

TEST(SprofilerUpload, FailedUploadKeepsPending) {
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

TEST(SprofilerUpload, NoPendingShotReturns) {
  auto u = make_uploader();
  EXPECT_FALSE(u.upload_pending_shot());
  EXPECT_EQ(u.post_call_count, 0);
}

TEST(SprofilerUpload, CorrectUrlConstructed) {
  auto u = make_uploader("https://my.server.io", "tok123", "Profile");
  g_mock_millis = 0;

  u.begin_shot();
  u.add_datapoint(0.0f, 0.0f, 90.0f, 0.0f, 0.0f);
  u.end_shot(1000);
  u.upload_pending_shot();

  EXPECT_EQ(u.last_url, "https://my.server.io/api/shots/upload");
}

TEST(SprofilerUpload, TrailingSlashUrlNormalized) {
  auto u = make_uploader("https://test.io/", "tok", "P");
  g_mock_millis = 0;

  u.begin_shot();
  u.add_datapoint(0.0f, 0.0f, 90.0f, 0.0f, 0.0f);
  u.end_shot(1000);
  u.upload_pending_shot();

  // Trailing slash should be normalised — no double slash.
  EXPECT_EQ(u.last_url, "https://test.io/api/shots/upload");
}

TEST(SprofilerUpload, BearerTokenInAuthHeader) {
  auto u = make_uploader("https://s.io", "secret_token_xyz", "P");
  g_mock_millis = 0;

  u.begin_shot();
  u.add_datapoint(0.0f, 0.0f, 90.0f, 0.0f, 0.0f);
  u.end_shot(1000);
  u.upload_pending_shot();

  EXPECT_EQ(u.last_auth, "Bearer secret_token_xyz");
}

TEST(SprofilerUpload, BodyContainsSerializedJson) {
  auto u = make_uploader();
  g_mock_millis = 0;

  u.begin_shot();
  u.add_datapoint(0.0f, 9.0f, 93.0f, 2.5f, 0.0f);
  u.end_shot(25000);
  u.upload_pending_shot();

  EXPECT_FALSE(u.last_body.empty());
  EXPECT_NE(u.last_body.find("\"datapoints\""), std::string::npos);
  EXPECT_NE(u.last_body.find("\"duration\":25000"), std::string::npos);
}

TEST(SprofilerUpload, TransportErrorKeepsPending) {
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

TEST(SprofilerUpload, RetrySucceedsOnSecondAttempt) {
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

TEST(SprofilerUpload, Http200And201BothSucceed) {
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

TEST(SprofilerUpload, Http4xxFailsUpload) {
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

TEST(SprofilerLoop, LoopUploadsWhenPending) {
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

TEST(SprofilerLoop, LoopDoesNothingWhenNoPending) {
  auto u = make_uploader();
  u.loop();
  EXPECT_EQ(u.post_call_count, 0);
}

// ===========================================================================
// Edge cases
// ===========================================================================

TEST(SprofilerEdge, LargeShotSerializes) {
  auto u = make_uploader();
  g_mock_millis = 0;
  u.begin_shot();

  for (int i = 0; i < 300; i++) {
    float t = i * 0.1f;
    u.add_datapoint(t, 9.0f, 93.0f, 2.5f, t * 1.2f);
  }
  u.end_shot(30000);

  std::string json = u.serialize_shot_json();
  EXPECT_FALSE(json.empty());
  size_t count = 0;
  size_t pos = 0;
  while ((pos = json.find("\"time\":", pos)) != std::string::npos) {
    count++;
    pos++;
  }
  EXPECT_EQ(count, 300u);
}

TEST(SprofilerEdge, EmptyApiTokenStillUploads) {
  MockSprofilerUpload u;
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
// Auto-recording — sprofiler wired to EspressoMachine orchestrator
// ===========================================================================

// Helper: set up a minimal orchestrator with mock hardware.
struct SprofilerOrchestratorFixture {
  MockValve brew_valve;
  MockValve purge_valve;
  MockValve steam_valve;
  MockValve steam_purge_valve;
  MockPump brew_pump;
  MockPump steam_pump;
  MockHeater heater;
  EspressoMachine machine;
  MockSprofilerUpload uploader;

  SprofilerOrchestratorFixture() {
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

    uploader.set_server_url("https://test.sprofiler.io");
    uploader.set_api_token("test_token");
    uploader.set_profile_name("Test");
    uploader.set_machine(&machine);
    uploader.mock_http_status = 200;

    g_mock_millis = 0;
    machine.setup();
    machine.machine_on();
  }
};

TEST(SprofilerAutoRecord, StartsRecordingOnBrewStart) {
  SprofilerOrchestratorFixture f;

  // Heater already at brew temperature (bypass HEATING).
  f.heater.temp = 90.0f;
  f.machine.brew_start();
  f.machine.loop();

  // Sprofiler loop should detect mode == BREWING and start recording.
  f.uploader.loop();

  EXPECT_TRUE(f.uploader.is_recording());
  EXPECT_EQ(f.uploader.get_shot_id(), 1u);
}

TEST(SprofilerAutoRecord, SamplesDatapointsDuringBrew) {
  SprofilerOrchestratorFixture f;
  f.heater.temp = 90.0f;
  f.machine.brew_start();
  f.machine.loop();
  f.uploader.loop();  // Rising edge detected, recording starts.

  // Advance time and run loop ticks to accumulate datapoints.
  for (int i = 1; i <= 5; i++) {
    g_mock_millis = i * 100;
    f.brew_pump.rate = 2.5f;
    f.machine.loop();
    f.uploader.loop();
  }

  // 5 ticks at 100ms intervals → 5 datapoints (first sample at tick 1).
  EXPECT_GE(f.uploader.get_datapoints().size(), 5u);
}

TEST(SprofilerAutoRecord, DatapointsContainTemperatureAndFlow) {
  SprofilerOrchestratorFixture f;
  f.heater.temp = 93.0f;
  f.machine.brew_start();
  f.machine.loop();
  f.uploader.loop();  // Rising edge → recording starts.

  // Set flow rate AFTER brew enters BREWING (orchestrator resets flow on start).
  f.brew_pump.rate = 2.5f;
  g_mock_millis = 100;
  f.machine.loop();
  f.uploader.loop();

  ASSERT_GE(f.uploader.get_datapoints().size(), 1u);
  const auto &dp = f.uploader.get_datapoints().back();
  EXPECT_FLOAT_EQ(dp.temperature_c, 93.0f);
  EXPECT_FLOAT_EQ(dp.flow_ml_s, 2.5f);
}

TEST(SprofilerAutoRecord, EndsRecordingOnBrewStop) {
  SprofilerOrchestratorFixture f;
  f.heater.temp = 90.0f;
  f.machine.brew_start();
  f.machine.loop();
  f.uploader.loop();  // Recording starts.

  g_mock_millis = 100;
  f.machine.loop();
  f.uploader.loop();

  // Stop the brew.
  f.machine.brew_stop();
  g_mock_millis = 200;
  f.machine.loop();
  f.uploader.loop();  // Falling edge → end_shot() + upload in same tick.

  EXPECT_FALSE(f.uploader.is_recording());
  // The upload happens in the same loop() tick as end_shot(), so the shot
  // has already been posted and pending is cleared.
  EXPECT_EQ(f.uploader.post_call_count, 1);
  EXPECT_FALSE(f.uploader.has_pending_upload());
}

TEST(SprofilerAutoRecord, UploadsAfterBrewEnds) {
  SprofilerOrchestratorFixture f;
  f.heater.temp = 90.0f;
  f.machine.brew_start();
  f.machine.loop();
  f.uploader.loop();

  g_mock_millis = 100;
  f.machine.loop();
  f.uploader.loop();

  f.machine.brew_stop();
  g_mock_millis = 200;
  f.machine.loop();
  f.uploader.loop();  // Ends shot.

  g_mock_millis = 300;
  f.uploader.loop();  // Uploads.

  EXPECT_EQ(f.uploader.post_call_count, 1);
  EXPECT_FALSE(f.uploader.has_pending_upload());
}

TEST(SprofilerAutoRecord, EndsOnFlowMaxAutoTermination) {
  SprofilerOrchestratorFixture f;
  f.heater.temp = 90.0f;
  f.machine.brew_start();
  f.machine.loop();
  f.uploader.loop();  // Recording starts.

  // Collect at least one datapoint before flow_max.
  f.brew_pump.rate = 5.0f;
  g_mock_millis = 100;
  f.machine.loop();
  f.uploader.loop();

  // Simulate reaching flow_max (40 ml).
  f.brew_pump.volume = 40.0f;
  g_mock_millis = 1000;
  f.machine.loop();  // Orchestrator transitions to DONE.
  f.uploader.loop();

  g_mock_millis = 1100;
  f.machine.loop();  // DONE → CLEANUP.
  f.uploader.loop();

  g_mock_millis = 1200;
  f.machine.loop();  // CLEANUP → IDLE.
  f.uploader.loop();  // Falling edge → end_shot() + upload.

  EXPECT_FALSE(f.uploader.is_recording());
  // The shot was ended and uploaded in the same loop() tick after the
  // orchestrator transitioned back to IDLE.
  EXPECT_EQ(f.uploader.post_call_count, 1);
  EXPECT_FALSE(f.uploader.has_pending_upload());
}

TEST(SprofilerAutoRecord, NoRecordingWithoutMachine) {
  MockSprofilerUpload u;
  u.set_server_url("https://test.io");
  u.set_api_token("tok");
  // No set_machine() call — sprofiler has no orchestrator reference.

  g_mock_millis = 0;
  u.loop();
  g_mock_millis = 100;
  u.loop();

  EXPECT_FALSE(u.is_recording());
  EXPECT_EQ(u.get_shot_id(), 0u);
}

TEST(SprofilerAutoRecord, NoRecordingDuringSteam) {
  SprofilerOrchestratorFixture f;
  f.heater.temp = 135.0f;

  f.machine.steam_start();
  f.machine.loop();
  f.uploader.loop();

  // Steaming is not brew — sprofiler should not start recording.
  EXPECT_FALSE(f.uploader.is_recording());
  EXPECT_EQ(f.uploader.get_shot_id(), 0u);
}

TEST(SprofilerAutoRecord, SecondBrewCreatesNewShot) {
  SprofilerOrchestratorFixture f;
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

  // Upload first shot.
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
