#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <cmath>
#include <initializer_list>
#include <vector>

#include "ClockSync.h"
#include "SpscQueue.h"

using piko::ClockDiagnostics;
using piko::ClockEvent;
using piko::ClockEventType;
using piko::ClockState;
using piko::ClockSync;

namespace {

// Mirrors the firmware: capture events are processed inside the PWM carrier
// IRQ, so a beat raised at an edge is emitted on the tick that sees it.
class Sim {
 public:
  explicit Sim(uint8_t ppqn, bool restart_on_start = true) : clock_(1000000u) {
    clock_.setPulsePpqn(ppqn);
    clock_.setRestartOnStart(restart_on_start);
  }

  ClockSync& clock() { return clock_; }
  const std::vector<uint32_t>& beats() const { return beats_; }
  uint32_t now() const { return now_; }
  void clearBeats() { beats_.clear(); }

  void advanceTo(uint32_t target) {
    while (now_ != target) {
      const uint32_t remaining = target - now_;
      now_ += remaining < kStepUs ? remaining : kStepUs;
      if (clock_.advanceCarrier(now_)) beats_.push_back(now_);
    }
  }

  void pulse(uint32_t timestamp_us) {
    advanceTo(timestamp_us);
    clock_.process({ClockEventType::Pulse, timestamp_us});
    if (clock_.advanceCarrier(timestamp_us)) beats_.push_back(timestamp_us);
  }

  void reset(uint32_t timestamp_us) {
    advanceTo(timestamp_us);
    clock_.process({ClockEventType::Reset, timestamp_us});
    if (clock_.advanceCarrier(timestamp_us)) beats_.push_back(timestamp_us);
  }

  bool running() const { return clock_.state() == ClockState::Running; }

 private:
  static constexpr uint32_t kStepUs = 50u;
  ClockSync clock_;
  uint32_t now_ = 0;
  std::vector<uint32_t> beats_;
};

bool near(uint32_t a, uint32_t b, uint32_t tolerance) {
  return (a > b ? a - b : b - a) <= tolerance;
}

// Every division must place eighth-note beats on the incoming edges, with no
// prediction and no drift, at a steady 120 BPM.
void testFixedTempoLandmarksPerDivision() {
  for (const uint8_t ppqn : {1, 2, 4, 8, 12, 24, 48}) {
    Sim sim(ppqn);
    const uint32_t quarter_us = 500000u;
    const uint32_t interval = quarter_us / ppqn;
    const uint32_t quarters = 4u;
    for (uint32_t i = 0; i <= quarters * ppqn; ++i) {
      sim.pulse(i * interval);
    }
    sim.advanceTo(quarters * ppqn * interval + interval * ppqn / 2u);

    // One beat per eighth note, landing exactly on the pulse that marks it. A
    // 1 PPQN clock has no tempo estimate at its very first pulse, so that one
    // off-beat eighth cannot be interpolated; the rest are.
    std::vector<uint32_t> expected;
    if (ppqn == 1) {
      expected.push_back(0u);
      for (uint32_t q = 1; q <= quarters; ++q) {
        expected.push_back(q * interval);
        expected.push_back(q * interval + interval / 2u);
      }
    } else {
      const uint32_t pulses_per_eighth = ppqn / 2u;
      for (uint32_t i = 0; i <= quarters * 2u; ++i) {
        expected.push_back(i * pulses_per_eighth * interval);
      }
    }
    assert(sim.beats().size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
      // Interpolated eighths land on the carrier tick after their due time.
      assert(near(sim.beats()[i], expected[i], ppqn == 1 ? 100u : 0u));
    }
    const ClockDiagnostics d = sim.clock().diagnostics();
    assert(d.state == ClockState::Running);
    assert(d.pulse_ppqn == ppqn);
    assert(near(d.bpm_x100, 12000u, 20u));
  }
}

// The estimate is filtered, so a tempo step is followed within an eighth or so
// rather than instantly.
void testTempoRampTrackingLag() {
  Sim sim(24);
  uint32_t now = 0;
  const uint32_t slow_interval = 500000u / 24u;  // 120 BPM
  for (uint32_t i = 0; i < 48; ++i) {
    sim.pulse(now);
    now += slow_interval;
  }
  assert(near(sim.clock().diagnostics().bpm_x100, 12000u, 50u));

  const uint32_t fast_interval = 500000u / 24u * 120u / 140u;  // ~140 BPM
  sim.pulse(now);
  now += fast_interval;
  sim.pulse(now);
  now += fast_interval;
  // Two pulses into the change the estimate has barely moved.
  assert(sim.clock().diagnostics().bpm_x100 < 12500u);

  for (uint32_t i = 0; i < 48; ++i) {
    sim.pulse(now);
    now += fast_interval;
  }
  assert(near(sim.clock().diagnostics().bpm_x100, 14000u, 200u));
}

// Swung input keeps uneven spacing; beats must land on the edges and the
// transport must not mistake the long half for a stop.
void testSwingPulsesStayOnEdges() {
  Sim sim(2);
  std::vector<uint32_t> edges;
  uint32_t now = 0;
  for (uint32_t i = 0; i < 16; ++i) {
    edges.push_back(now);
    sim.pulse(now);
    now += (i % 2 == 0) ? 300000u : 200000u;
  }
  sim.advanceTo(now);
  assert(sim.running());
  assert(sim.beats().size() == edges.size());
  for (size_t i = 0; i < edges.size(); ++i) {
    assert(sim.beats()[i] == edges[i]);
  }
}

// One pulse per quarter: the pulse is the on-beat eighth and the off-beat is
// generated from phase, unless the next pulse gets there first.
void testQuarterPulseIntermediateBeat() {
  Sim sim(1);
  sim.pulse(0);
  sim.pulse(500000u);
  sim.advanceTo(999000u);
  // Beats: the two pulses plus the interpolated eighth after the second. The
  // first pulse has no tempo estimate yet, so it interpolates nothing.
  assert(sim.beats().size() == 3u);
  assert(sim.beats()[0] == 0u);
  assert(sim.beats()[1] == 500000u);
  assert(near(sim.beats()[2], 750000u, 100u));

  sim.pulse(1000000u);
  sim.clearBeats();
  // The next pulse arrives before the interpolated eighth would have.
  sim.pulse(1100000u);
  assert(sim.beats().size() == 1u);
  assert(sim.beats()[0] == 1100000u);
}

void testStopDetection() {
  // Adaptive threshold: 2.5 intervals at 120 BPM / 2 PPQN is 625 ms.
  Sim sim(2);
  for (uint32_t i = 0; i < 8; ++i) sim.pulse(i * 250000u);
  const uint32_t last_edge = 7u * 250000u;
  sim.advanceTo(last_edge + 600000u);
  assert(sim.running());
  sim.advanceTo(last_edge + 700000u);
  assert(!sim.running());

  // Floor: a fast clock still waits 250 ms before declaring a stop.
  Sim fast(48);
  const uint32_t fast_interval = 200000u / 48u;  // 300 BPM
  for (uint32_t i = 0; i < 96; ++i) fast.pulse(i * fast_interval);
  const uint32_t fast_last = 95u * fast_interval;
  fast.advanceTo(fast_last + 240000u);
  assert(fast.running());
  fast.advanceTo(fast_last + 260000u);
  assert(!fast.running());

  // Before the first interval the threshold comes from the division: a 1 PPQN
  // clock is allowed a full 30 BPM quarter (2 s), times 2.5.
  Sim lonely(1);
  lonely.pulse(0);
  lonely.advanceTo(4900000u);
  assert(lonely.running());
  lonely.advanceTo(5100000u);
  assert(!lonely.running());
}

// 30 BPM on a quarter-note clock: two seconds between the first two pulses is
// normal, not a stop.
void testSlowClockIsNotAStop() {
  Sim sim(1);
  sim.pulse(0);
  sim.pulse(2000000u);
  assert(sim.running());
  assert(sim.beats().size() == 2u);

  // A clock that keeps slowing below 30 BPM must still not drop out.
  Sim slowing(2);
  uint32_t now = 0;
  uint32_t interval = 250000u;
  for (uint32_t i = 0; i < 12; ++i) {
    slowing.pulse(now);
    assert(slowing.running());
    now += interval;
    interval = interval * 3u / 2u;  // each gap 50 % longer than the last
  }
  slowing.pulse(now);
  assert(slowing.running());
  assert(slowing.beats().size() == 13u);
}

void testRestartOnStart() {
  // Enabled: the first pulse after a stop restarts from the first beat.
  Sim sim(4);
  for (uint32_t i = 0; i < 5; ++i) sim.pulse(i * 125000u);
  assert(sim.clock().consumePositionReset());  // initial start
  sim.advanceTo(4u * 125000u + 1000000u);
  assert(!sim.running());
  sim.clearBeats();
  sim.pulse(sim.now() + 100000u);
  assert(sim.clock().consumePositionReset());
  assert(sim.beats().size() == 1u);
  assert(sim.clock().diagnostics().restart_count == 2u);

  // Disabled: position and landmark counting continue where they stopped.
  Sim keep(4, false);
  for (uint32_t i = 0; i < 5; ++i) keep.pulse(i * 125000u);  // indices 0..4
  assert(!keep.clock().consumePositionReset());
  keep.advanceTo(4u * 125000u + 1000000u);
  assert(!keep.running());
  keep.clearBeats();
  keep.pulse(keep.now() + 100000u);  // index 5: not an eighth landmark
  assert(!keep.clock().consumePositionReset());
  assert(keep.beats().empty());
  keep.pulse(keep.now() + 125000u);  // index 6: landmark
  assert(keep.beats().size() == 1u);
  assert(keep.clock().diagnostics().restart_count == 0u);
}

void testResetInput() {
  // A reset just after a pulse belongs to that pulse and acts immediately.
  Sim sim(4);
  for (uint32_t i = 0; i < 4; ++i) sim.pulse(i * 125000u);
  (void)sim.clock().consumePositionReset();
  sim.clearBeats();
  const uint32_t pulse_us = 4u * 125000u;
  sim.pulse(pulse_us);
  sim.reset(pulse_us + 2000u);
  assert(sim.clock().consumePositionReset());
  assert(sim.beats().size() == 2u);  // the pulse, then the reset
  assert(sim.beats()[1] == pulse_us + 2000u);
  // Landmarks now count from that pulse: the next one is index 1.
  sim.clearBeats();
  sim.pulse(pulse_us + 125000u);
  assert(sim.beats().empty());
  sim.pulse(pulse_us + 250000u);
  assert(sim.beats().size() == 1u);

  // A reset outside that window waits for the next pulse.
  Sim pending(4);
  for (uint32_t i = 0; i < 5; ++i) pending.pulse(i * 125000u);
  (void)pending.clock().consumePositionReset();
  pending.clearBeats();
  pending.reset(4u * 125000u + 60000u);
  assert(!pending.clock().consumePositionReset());
  assert(pending.beats().empty());
  pending.pulse(5u * 125000u);
  assert(pending.clock().consumePositionReset());
  assert(pending.beats().size() == 1u);

  // A reset while stopped is applied on the pulse that starts the clock again.
  Sim stopped(4, false);
  stopped.pulse(0);
  // With no interval measured yet the threshold is 2.5 pulse periods at 30 BPM.
  stopped.advanceTo(1400000u);
  assert(!stopped.running());
  stopped.reset(1500000u);
  assert(!stopped.clock().consumePositionReset());
  stopped.clearBeats();
  stopped.pulse(1600000u);
  assert(stopped.clock().consumePositionReset());
  assert(stopped.beats().size() == 1u);
  assert(stopped.clock().diagnostics().reset_count == 1u);
}

void testQueueOverflow() {
  SpscQueue<uint32_t, 4> queue;
  assert(queue.push(1));
  assert(queue.push(2));
  assert(queue.push(3));
  assert(!queue.push(4));
  assert(queue.drops() == 1u);
  uint32_t value = 0;
  assert(queue.pop(value) && value == 1u);
  assert(queue.push(5));
  assert(queue.pop(value) && value == 2u);
  assert(queue.pop(value) && value == 3u);
  assert(queue.pop(value) && value == 5u);
  assert(!queue.pop(value));
}

void testTempoClamping() {
  // Far below 30 BPM the playback rate clamps instead of crawling.
  Sim slow(1);
  slow.pulse(0);
  slow.pulse(4000000u);  // 15 BPM
  assert(slow.clock().targetBpmX100() == 3000u);

  // Far above 300 BPM it clamps at the top.
  Sim fast(1);
  fast.pulse(0);
  fast.pulse(100000u);  // 600 BPM
  assert(fast.clock().targetBpmX100() == 30000u);
}

void testPlaybackRatios() {
  constexpr uint32_t carrier = 1000000u;
  const uint64_t unity = ClockSync::playbackIncrementQ32(carrier, 12000, 120);
  const uint64_t double_speed =
      ClockSync::playbackIncrementQ32(carrier, 24000, 120);
  const uint64_t half_speed =
      ClockSync::playbackIncrementQ32(carrier, 6000, 120);
  assert(double_speed == unity * 2u || double_speed == unity * 2u - 1u ||
         double_speed == unity * 2u + 1u);
  assert(half_speed == unity / 2u || half_speed == unity / 2u + 1u);

  // Exercise loader BPM/beat-count combinations. Beat count changes slicing
  // only, while source BPM determines the fractional playback rate.
  for (const uint32_t target_bpm_x100 : {3000u, 12000u, 24000u, 30000u}) {
    for (const uint32_t source_bpm : {90u, 120u, 165u, 200u}) {
      uint64_t rate_for_first_beat_count = 0;
      for (const uint32_t beat_count : {1u, 8u, 16u, 128u}) {
        (void)beat_count;
        const uint64_t rate = ClockSync::playbackIncrementQ32(
            carrier, target_bpm_x100, source_bpm);
        if (rate_for_first_beat_count == 0) {
          rate_for_first_beat_count = rate;
        } else {
          assert(rate == rate_for_first_beat_count);
        }
        const long double actual_hz =
            static_cast<long double>(rate) * carrier / (1ull << 32u);
        const long double expected_hz =
            24000.0L * target_bpm_x100 / (100.0L * source_bpm);
        assert(std::abs(actual_hz - expected_hz) < 0.001L);
      }
    }
  }
}

void testValidDivisions() {
  for (const uint8_t ppqn : {1, 2, 4, 8, 12, 24, 48}) {
    assert(ClockSync::validPulsePpqn(ppqn));
  }
  for (const uint8_t ppqn : {0, 3, 5, 6, 16, 32, 96}) {
    assert(!ClockSync::validPulsePpqn(ppqn));
  }
  ClockSync clock(1000000u);
  assert(clock.pulsePpqn() == 24u);
  assert(clock.restartOnStart());
  assert(clock.state() == ClockState::Stopped);
}

}  // namespace

int main() {
  testValidDivisions();
  testFixedTempoLandmarksPerDivision();
  testTempoRampTrackingLag();
  testSwingPulsesStayOnEdges();
  testQuarterPulseIntermediateBeat();
  testStopDetection();
  testSlowClockIsNotAStop();
  testRestartOnStart();
  testResetInput();
  testQueueOverflow();
  testTempoClamping();
  testPlaybackRatios();
  puts("clock_sync_test: all tests passed");
  return 0;
}
