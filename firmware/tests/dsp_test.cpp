#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <cmath>
#include <vector>

#include "PikoProbabilities.h"
#include "doth/djfilter.h"

#define FLASH_PAGE_SIZE 256u
#include "doth/sequencer.h"

namespace {

constexpr double kSampleRate = PIKO_DJ_FILTER_SAMPLE_RATE;

uint8_t encode(double value) {
  int32_t sample = static_cast<int32_t>(std::lround(value)) + 128;
  if (sample < 0) sample = 0;
  if (sample > 255) sample = 255;
  return static_cast<uint8_t>(sample);
}

// Runs a sine through the filter at a fixed position and returns the peak
// amplitude of the settled output.
double responseAmplitude(int32_t position_q16, double frequency_hz,
                         double amplitude) {
  DjFilter filter;
  dj_filter_init(&filter);
  filter.target = position_q16;
  filter.position = position_q16;

  const uint32_t settle = 12000;
  const uint32_t measure = 4800;
  double peak = 0.0;
  for (uint32_t i = 0; i < settle + measure; ++i) {
    const double phase = 2.0 * M_PI * frequency_hz * i / kSampleRate;
    const uint8_t in = encode(amplitude * std::sin(phase));
    const uint8_t out = dj_filter_process(&filter, in, position_q16);
    if (i >= settle) {
      const double centred = static_cast<double>(out) - 128.0;
      if (std::fabs(centred) > peak) peak = std::fabs(centred);
    }
  }
  return peak;
}

void testBypassIsBitExact() {
  DjFilter filter;
  dj_filter_init(&filter);
  // The middle of the pot, and the whole documented bypass window.
  for (const uint16_t knob :
       {(uint16_t)PIKO_DJ_BYPASS_LO, (uint16_t)2048,
        (uint16_t)PIKO_DJ_BYPASS_HI}) {
    assert(dj_filter_position_from_knob(knob) == 0);
  }
  dj_filter_set_knob(&filter, 2048);
  for (uint32_t i = 0; i < 4; ++i) {
    for (uint32_t value = 0; value <= 255; ++value) {
      dj_filter_advance(&filter);
      const uint8_t sample = static_cast<uint8_t>(value);
      assert(dj_filter_process(&filter, sample, filter.position) == sample);
    }
  }
  // Bypass also clears the state, so nothing rings out of a previous setting.
  assert(filter.stage[0].ic1eq == 0 && filter.stage[0].ic2eq == 0);
  assert(filter.stage[1].ic1eq == 0 && filter.stage[1].ic2eq == 0);
}

void testLowpassEndKillsHighs() {
  const double amplitude = 100.0;
  const int32_t closed = -PIKO_DJ_POSITION_ONE;  // 60 Hz low-pass
  // A 6 kHz tone is two decades above the cutoff: 4th order leaves nothing.
  assert(responseAmplitude(closed, 6000.0, amplitude) < amplitude * 0.02);
  // 1 kHz is still far above 60 Hz and must be strongly attenuated.
  assert(responseAmplitude(closed, 1000.0, amplitude) < amplitude * 0.05);
  // A 30 Hz tone passes essentially untouched.
  assert(responseAmplitude(closed, 30.0, amplitude) > amplitude * 0.9);

  // At the bypass edge the low-pass is wide open at 10 kHz.
  const int32_t edge = -1;
  assert(responseAmplitude(edge, 1000.0, amplitude) > amplitude * 0.9);
}

void testHighpassEndKillsLows() {
  const double amplitude = 100.0;
  const int32_t open = PIKO_DJ_POSITION_ONE;  // 6 kHz high-pass
  assert(responseAmplitude(open, 100.0, amplitude) < amplitude * 0.02);
  assert(responseAmplitude(open, 1000.0, amplitude) < amplitude * 0.3);
  // Near Nyquist the high-pass passes the tone.
  assert(responseAmplitude(open, 10000.0, amplitude) > amplitude * 0.7);

  // At the bypass edge the high-pass sits at 30 Hz, so audio passes.
  const int32_t edge = 1;
  assert(responseAmplitude(edge, 1000.0, amplitude) > amplitude * 0.9);
}

// Sweeping the pot from end to end must not produce steps in the output.
void testSweepHasNoDiscontinuity() {
  DjFilter filter;
  dj_filter_init(&filter);
  const double amplitude = 100.0;
  const double frequency = 220.0;
  const uint32_t samples = 240000;  // ten seconds of sweeping
  int32_t previous = 0;
  int32_t worst_step = 0;
  for (uint32_t i = 0; i < samples; ++i) {
    const uint16_t knob =
        static_cast<uint16_t>((uint64_t)i * PIKO_DJ_KNOB_MAX / (samples - 1));
    dj_filter_set_knob(&filter, knob);
    dj_filter_advance(&filter);
    const double phase = 2.0 * M_PI * frequency * i / kSampleRate;
    const uint8_t in = encode(amplitude * std::sin(phase));
    const int32_t out = dj_filter_process(&filter, in, filter.position);
    if (i > 0) {
      const int32_t step = out > previous ? out - previous : previous - out;
      if (step > worst_step) worst_step = step;
    }
    previous = out;
  }
  // A 220 Hz tone at this amplitude moves at most ~6 LSB per sample on its
  // own; anything much larger would be a jump caused by the sweep.
  assert(worst_step <= 10);
}

void testRetrigSweepUsesLowpassArm() {
  // The effect pushes the position further into the low-pass half; the deeper
  // it goes, the less high frequency survives.
  const double amplitude = 100.0;
  const double open = responseAmplitude(-PIKO_DJ_POSITION_ONE / 8, 2000.0,
                                        amplitude);
  const double closed = responseAmplitude(-PIKO_DJ_POSITION_ONE, 2000.0,
                                          amplitude);
  assert(closed < open);
  assert(closed < amplitude * 0.05);
}

void testSequencerRecordStopsAtCapacity() {
  Sequencer sequencer;
  sequencer.Init();
  sequencer.SetRecording(true);
  for (uint32_t i = 0; i < 300; ++i) {
    sequencer.Record(static_cast<uint8_t>(i % 8));
  }
  // 128 steps fit; the rest are dropped instead of running off the array.
  uint8_t page[FLASH_PAGE_SIZE] = {0};
  sequencer.Save(page);
  assert(page[98] == 128u);
  for (uint32_t i = 0; i < 128; ++i) {
    assert(page[100 + i] == static_cast<uint8_t>(i % 8));
  }
  assert(sequencer.Last() == static_cast<uint8_t>(127 % 8));
}

void testProbabilitiesRoundTrip() {
  uint8_t page[FLASH_PAGE_SIZE] = {0};
  const PikoProbabilities stored{11, 22, 33, 44, 55};
  piko_store_probabilities(stored, page);

  // Each probability owns its own byte: no two share a slot.
  assert(page[PIKO_SAVE_PROB_DIRECTION] == 11u);
  assert(page[PIKO_SAVE_PROB_JUMP] == 22u);
  assert(page[PIKO_SAVE_PROB_RETRIG] == 33u);
  assert(page[PIKO_SAVE_PROB_GATE] == 44u);
  assert(page[PIKO_SAVE_PROB_TUNNEL] == 55u);

  const PikoProbabilities loaded = piko_load_probabilities(page);
  assert(loaded.direction == stored.direction);
  assert(loaded.jump == stored.jump);
  assert(loaded.retrig == stored.retrig);
  assert(loaded.gate == stored.gate);
  assert(loaded.tunnel == stored.tunnel);
}

}  // namespace

int main() {
  testBypassIsBitExact();
  testLowpassEndKillsHighs();
  testHighpassEndKillsLows();
  testSweepHasNoDiscontinuity();
  testRetrigSweepUsesLowpassArm();
  testSequencerRecordStopsAtCapacity();
  testProbabilitiesRoundTrip();
  puts("dsp_test: all tests passed");
  return 0;
}
