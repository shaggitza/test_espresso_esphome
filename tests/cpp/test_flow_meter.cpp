#include <gtest/gtest.h>
#include "esphome/core/hal.h"
#include "espresso_machine_flow_meter/flow_meter.h"

using namespace esphome::espresso_machine_flow_meter;

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
