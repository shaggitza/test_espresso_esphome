// Unit tests for the Sprofiler shot upload component.
//
// All HTTP transport is mocked — no real network calls are made.  The tests
// exercise shot data collection, JSON serialisation (Gaggiuino format), and
// the upload retry / success / failure paths.

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "components/espresso_machine_sprofiler/sprofiler.h"

// Pull in the mock millis() state.
extern uint32_t g_mock_millis;

using esphome::espresso_machine_sprofiler::ShotDatapoint;
using esphome::espresso_machine_sprofiler::SprofilerShotUpload;

// ---------------------------------------------------------------------------
// MockSprofilerUpload — captures http_post calls for verification
// ---------------------------------------------------------------------------
class MockSprofilerUpload : public SprofilerShotUpload {
 public:
  // Predetermined HTTP status code returned by http_post().
  int mock_http_status{200};

  // Captured values from the last http_post() call.
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
  // Not recording — datapoints should be silently dropped.
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

  // Start a new shot — previous datapoints should be cleared.
  u.begin_shot();
  EXPECT_TRUE(u.get_datapoints().empty());
  EXPECT_FALSE(u.has_pending_upload());
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
  g_mock_millis = 1000000;  // timestamp = 1000
  u.begin_shot();
  u.add_datapoint(0.0f, 0.1f, 92.5f, 7.0f, 0.0f);
  u.end_shot(25000);

  std::string json = u.serialize_shot_json();

  // Verify top-level fields.
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

  // Count the number of datapoint objects.
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
  u.mock_http_status = 201;  // HTTP 201 Created
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
  u.mock_http_status = 500;  // Server error
  g_mock_millis = 0;

  u.begin_shot();
  u.add_datapoint(0.0f, 9.0f, 93.0f, 2.5f, 0.0f);
  u.end_shot(25000);

  bool ok = u.upload_pending_shot();
  EXPECT_FALSE(ok);
  EXPECT_TRUE(u.has_pending_upload());  // Still pending for retry.
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

  // The body sent to http_post should match serialize_shot_json().
  // Since upload clears pending, we re-serialize from the still-present datapoints.
  EXPECT_FALSE(u.last_body.empty());
  EXPECT_NE(u.last_body.find("\"datapoints\""), std::string::npos);
  EXPECT_NE(u.last_body.find("\"duration\":25000"), std::string::npos);
}

TEST(SprofilerUpload, TransportErrorKeepsPending) {
  auto u = make_uploader();
  u.mock_http_status = -1;  // Transport failure (no connection)
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

  // First attempt fails.
  u.mock_http_status = 503;
  EXPECT_FALSE(u.upload_pending_shot());
  EXPECT_TRUE(u.has_pending_upload());

  // Second attempt succeeds.
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
// Loop integration
// ===========================================================================

TEST(SprofilerLoop, LoopUploadsWhenPending) {
  auto u = make_uploader();
  u.mock_http_status = 200;
  g_mock_millis = 0;

  u.begin_shot();
  u.add_datapoint(0.0f, 0.0f, 90.0f, 0.0f, 0.0f);
  u.end_shot(1000);

  EXPECT_TRUE(u.has_pending_upload());
  u.loop();  // Should trigger upload.
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

  // Simulate a 30-second shot at 10 Hz = 300 datapoints.
  for (int i = 0; i < 300; i++) {
    float t = i * 0.1f;
    u.add_datapoint(t, 9.0f, 93.0f, 2.5f, t * 1.2f);
  }
  u.end_shot(30000);

  std::string json = u.serialize_shot_json();
  EXPECT_FALSE(json.empty());
  // Should contain all 300 datapoints.
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
  u.mock_http_status = 401;  // Expected: server rejects empty token.
  g_mock_millis = 0;

  u.begin_shot();
  u.add_datapoint(0.0f, 0.0f, 90.0f, 0.0f, 0.0f);
  u.end_shot(1000);
  u.upload_pending_shot();

  // The component still attempts the upload; the server rejects it.
  EXPECT_EQ(u.last_auth, "Bearer ");
  EXPECT_EQ(u.post_call_count, 1);
}

TEST(SprofilerEdge, ServerUrlWithTrailingSlash) {
  auto u = make_uploader("https://test.io/", "tok", "P");
  g_mock_millis = 0;

  u.begin_shot();
  u.add_datapoint(0.0f, 0.0f, 90.0f, 0.0f, 0.0f);
  u.end_shot(1000);
  u.upload_pending_shot();

  // URL should be constructed even with trailing slash (not ideal but functional).
  EXPECT_EQ(u.last_url, "https://test.io//api/shots/upload");
}
