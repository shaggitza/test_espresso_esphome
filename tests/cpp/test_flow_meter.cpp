#include <gtest/gtest.h>
#include "esphome/core/hal.h"
#include "espresso_machine_flow_meter/flow_meter.h"

using namespace esphome::espresso_machine_flow_meter;
using esphome::sensor::Sensor;

extern uint32_t g_mock_millis;

static FlowMeter make_flow_meter(GPIOPin &pin, float pulses_per_ml = 1.0f) {
  FlowMeter fm;
  fm.set_pin(&pin);
  fm.set_pulses_per_ml(pulses_per_ml);
  g_mock_millis = 0;
  fm.setup();
  return fm;
}

TEST(FlowMeter, InitialStateIsZero) {
  GPIOPin pin;
  FlowMeter fm = make_flow_meter(pin, 1.0f);
  EXPECT_FLOAT_EQ(fm.get_total_volume(), 0.0f);
  EXPECT_FLOAT_EQ(fm.get_rate(), 0.0f);
  EXPECT_FLOAT_EQ(fm.total_volume(), 0.0f);
}

TEST(FlowMeter, ResetClearsVolumeAndRate) {
  GPIOPin pin;
  FlowMeter fm = make_flow_meter(pin, 1.0f);
  fm.add_pulses(100);
  g_mock_millis = 1000;
  fm.loop();
  EXPECT_GT(fm.get_total_volume(), 0.0f);

  fm.reset();
  EXPECT_FLOAT_EQ(fm.get_total_volume(), 0.0f);
  EXPECT_FLOAT_EQ(fm.get_rate(), 0.0f);
}

TEST(FlowMeter, VolumeAccumulatesCorrectly) {
  // 100 pulses at 1.0 pulses/ml = 100 ml
  GPIOPin pin;
  FlowMeter fm = make_flow_meter(pin, 1.0f);
  fm.add_pulses(100);
  g_mock_millis = 1000;
  fm.loop();
  EXPECT_FLOAT_EQ(fm.get_total_volume(), 100.0f);
}

TEST(FlowMeter, PulsesPerMlScalesVolumeCorrectly) {
  // AB32 flow meter: ~0.5195 pulses/ml
  // 52 pulses / 0.5195 ≈ 100.096 ml
  GPIOPin pin;
  FlowMeter fm = make_flow_meter(pin, 0.5195f);
  fm.add_pulses(52);
  g_mock_millis = 1000;
  fm.loop();
  EXPECT_NEAR(fm.get_total_volume(), 52.0f / 0.5195f, 0.01f);
}

TEST(FlowMeter, RateCalculationOverInterval) {
  // 50 pulses in 1000 ms at 1.0 pulses/ml → rate = 50 ml/s
  GPIOPin pin;
  FlowMeter fm = make_flow_meter(pin, 1.0f);
  fm.add_pulses(50);
  g_mock_millis = 1000;
  fm.loop();
  EXPECT_FLOAT_EQ(fm.get_rate(), 50.0f);
}

TEST(FlowMeter, LoopSkipsIfIntervalTooShort) {
  // loop() should not update if < 100 ms have passed
  GPIOPin pin;
  FlowMeter fm = make_flow_meter(pin, 1.0f);
  fm.add_pulses(100);
  g_mock_millis = 50;  // only 50 ms since setup (last_update_ms_ = 0)
  fm.loop();
  EXPECT_FLOAT_EQ(fm.get_total_volume(), 0.0f);
}

TEST(FlowMeter, VolumeAccumulatesAcrossMultipleCalls) {
  GPIOPin pin;
  FlowMeter fm = make_flow_meter(pin, 1.0f);

  fm.add_pulses(20);
  g_mock_millis = 1000;
  fm.loop();
  EXPECT_FLOAT_EQ(fm.get_total_volume(), 20.0f);

  fm.add_pulses(30);  // 30 more pulses since last call
  g_mock_millis = 2000;
  fm.loop();
  EXPECT_FLOAT_EQ(fm.get_total_volume(), 50.0f);
}

TEST(FlowMeter, ResetAfterAccumulationStartsFresh) {
  GPIOPin pin;
  FlowMeter fm = make_flow_meter(pin, 1.0f);
  fm.add_pulses(100);
  g_mock_millis = 1000;
  fm.loop();
  fm.reset();

  // After reset, new pulses should count from zero
  fm.add_pulses(10);
  g_mock_millis = 2000;
  fm.loop();
  EXPECT_FLOAT_EQ(fm.get_total_volume(), 10.0f);
}

TEST(FlowMeter, CalibrateAdjustsPulsesPerMl) {
  // 100 pulses delivered; user measures 50 ml → 2.0 pulses/ml
  GPIOPin pin;
  FlowMeter fm = make_flow_meter(pin, 1.0f);
  fm.add_pulses(100);
  fm.calibrate(50.0f);

  // After calibration the meter should convert 100 pulses → 50 ml
  g_mock_millis = 1000;
  fm.loop();
  EXPECT_FLOAT_EQ(fm.get_total_volume(), 50.0f);
}

TEST(FlowMeter, CalibrateIgnoresZeroVolume) {
  // calibrate() with 0 ml must be a no-op (avoid division by zero)
  GPIOPin pin;
  FlowMeter fm = make_flow_meter(pin, 1.0f);
  fm.add_pulses(100);
  fm.calibrate(0.0f);

  g_mock_millis = 1000;
  fm.loop();
  // pulses_per_ml unchanged → 100 pulses = 100 ml
  EXPECT_FLOAT_EQ(fm.get_total_volume(), 100.0f);
}

TEST(FlowMeter, CalibrateIgnoresZeroPulses) {
  // calibrate() with no pulses counted must be a no-op
  GPIOPin pin;
  FlowMeter fm = make_flow_meter(pin, 1.0f);
  // Do not add any pulses
  fm.calibrate(50.0f);

  fm.add_pulses(10);
  g_mock_millis = 1000;
  fm.loop();
  // pulses_per_ml unchanged → 10 pulses = 10 ml
  EXPECT_FLOAT_EQ(fm.get_total_volume(), 10.0f);
}

TEST(FlowMeter, SensorsReceivePublishedValues) {
  // When sensor pointers are set, loop() should publish rate and total.
  GPIOPin pin;
  FlowMeter fm = make_flow_meter(pin, 1.0f);

  Sensor rate_sens;
  Sensor total_sens;
  fm.set_rate_sensor(&rate_sens);
  fm.set_total_sensor(&total_sens);

  fm.add_pulses(20);
  g_mock_millis = 1000;
  fm.loop();

  EXPECT_FLOAT_EQ(total_sens.state, 20.0f);
  EXPECT_FLOAT_EQ(rate_sens.state, 20.0f);  // 20 ml in 1 s
}

TEST(FlowMeter, AvgRate3sIsZeroInitially) {
  GPIOPin pin;
  FlowMeter fm = make_flow_meter(pin, 1.0f);
  EXPECT_FLOAT_EQ(fm.get_avg_rate_3s(), 0.0f);
}

TEST(FlowMeter, AvgRate3sMatchesRateForSingleSample) {
  // With one loop tick at 50 ml/s the avg should equal that rate.
  GPIOPin pin;
  FlowMeter fm = make_flow_meter(pin, 1.0f);
  fm.add_pulses(50);
  g_mock_millis = 1000;
  fm.loop();
  EXPECT_FLOAT_EQ(fm.get_avg_rate_3s(), fm.get_rate());
}

TEST(FlowMeter, AvgRate3sAveragesMultipleSamples) {
  // Two ticks: 20 ml/s then 40 ml/s → average = 30 ml/s
  GPIOPin pin;
  FlowMeter fm = make_flow_meter(pin, 1.0f);

  fm.add_pulses(20);
  g_mock_millis = 1000;
  fm.loop();  // rate = 20 ml/s

  fm.add_pulses(40);
  g_mock_millis = 2000;
  fm.loop();  // rate = 40 ml/s

  EXPECT_FLOAT_EQ(fm.get_avg_rate_3s(), 30.0f);
}

TEST(FlowMeter, AvgRate3sWindowEvictsOldSamples) {
  // Fill the 30-sample window with rate=10, then inject rate=40 for 30 more ticks.
  // After 30 new ticks the window should contain only 40 ml/s samples → avg ≈ 40.
  GPIOPin pin;
  FlowMeter fm = make_flow_meter(pin, 1.0f);

  // First 30 ticks at 10 ml/s (1000 ms each, 10 pulses each)
  for (int i = 1; i <= 30; i++) {
    fm.add_pulses(10);
    g_mock_millis = static_cast<uint32_t>(i * 1000);
    fm.loop();
  }

  // Next 30 ticks at 40 ml/s (1000 ms each, 40 pulses each)
  for (int i = 31; i <= 60; i++) {
    fm.add_pulses(40);
    g_mock_millis = static_cast<uint32_t>(i * 1000);
    fm.loop();
  }

  EXPECT_FLOAT_EQ(fm.get_avg_rate_3s(), 40.0f);
}

TEST(FlowMeter, AvgRate3sClearedOnReset) {
  GPIOPin pin;
  FlowMeter fm = make_flow_meter(pin, 1.0f);
  fm.add_pulses(50);
  g_mock_millis = 1000;
  fm.loop();
  EXPECT_GT(fm.get_avg_rate_3s(), 0.0f);

  fm.reset();
  EXPECT_FLOAT_EQ(fm.get_avg_rate_3s(), 0.0f);
}

TEST(FlowMeter, AvgRateSensorReceivesPublishedValues) {
  GPIOPin pin;
  FlowMeter fm = make_flow_meter(pin, 1.0f);

  Sensor avg_sens;
  fm.set_avg_rate_sensor(&avg_sens);

  fm.add_pulses(30);
  g_mock_millis = 1000;
  fm.loop();  // rate = 30 ml/s, avg (1 sample) = 30 ml/s

  EXPECT_FLOAT_EQ(avg_sens.state, 30.0f);
}
