#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <cmath>
#include <vector>

#include "TempoEngine.h"

using piko::kGlideLongTicks;
using piko::kGlideShortTicks;
using piko::kTempoQ16One;
using piko::kTicksPerBar;
using piko::TempoEngine;
using piko::TickPlan;

namespace {

uint32_t bpm(double value) {
  return static_cast<uint32_t>(value * kTempoQ16One + 0.5);
}

double toBpm(uint32_t tempo_q16) {
  return static_cast<double>(tempo_q16) / kTempoQ16One;
}

// Runs the engine for `ticks` and returns every plan.
std::vector<TickPlan> run(TempoEngine& engine, uint32_t ticks) {
  std::vector<TickPlan> plans;
  for (uint32_t i = 0; i < ticks; ++i) plans.push_back(engine.planNextTick());
  return plans;
}

// The transport decides when a bar line arrives; here we simply say so and
// let the glide start, the way the clock does.
void crossBarLine(TempoEngine& engine) {
  engine.startPendingGlide();
}

void testTickPeriodCarriesTheFraction() {
  TempoEngine engine;
  // 137.3 BPM: 18208.30... us per tick, so the fraction must be carried.
  engine.reset(bpm(137.3));
  const std::vector<TickPlan> plans = run(engine, kTicksPerBar);
  uint64_t total_us = 0;
  for (const TickPlan& plan : plans) total_us += plan.period_us;

  const double exact_bar_us = 60000000.0 / 137.3 * 4.0;
  assert(std::fabs(static_cast<double>(total_us) - exact_bar_us) <= 1.0);

  // Individual ticks are whole microseconds either side of the exact period.
  const double exact_tick_us = exact_bar_us / kTicksPerBar;
  for (const TickPlan& plan : plans) {
    assert(std::fabs(plan.period_us - exact_tick_us) < 1.5);
  }
}

void testGlideLengthSelection() {
  // |ln(T1/T0)| * 1.875 / 192 <= 0.0025 keeps the glide at two bars.
  assert(TempoEngine::glideTicksFor(bpm(120.0), bpm(154.0)) == kGlideShortTicks);
  assert(TempoEngine::glideTicksFor(bpm(154.0), bpm(120.0)) == kGlideShortTicks);
  // A wider jump needs four bars.
  assert(TempoEngine::glideTicksFor(bpm(120.0), bpm(160.0)) == kGlideLongTicks);
  assert(TempoEngine::glideTicksFor(bpm(90.0), bpm(174.0)) == kGlideLongTicks);
  // Four bars is the maximum even when the limit is still exceeded.
  assert(TempoEngine::glideTicksFor(bpm(40.0), bpm(300.0)) == kGlideLongTicks);
}

void testGlideCurve() {
  const uint32_t from = bpm(120.0);
  const uint32_t to = bpm(150.0);
  const uint32_t n = kGlideShortTicks;
  assert(TempoEngine::glideTempo(from, to, 0, n) == from);
  assert(TempoEngine::glideTempo(from, to, n, n) == to);

  // Halfway through smootherstep is 0.5, so the tempo is the geometric mean.
  const double mid = toBpm(TempoEngine::glideTempo(from, to, n / 2, n));
  assert(std::fabs(mid - std::sqrt(120.0 * 150.0)) < 0.05);

  // Monotone, and the ends are flat: the first step is far smaller than the
  // middle one.
  double previous = toBpm(TempoEngine::glideTempo(from, to, 0, n));
  double first_step = 0.0;
  double middle_step = 0.0;
  for (uint32_t k = 1; k <= n; ++k) {
    const double value = toBpm(TempoEngine::glideTempo(from, to, k, n));
    assert(value >= previous);
    if (k == 1) first_step = value - previous;
    if (k == n / 2) middle_step = value - previous;
    previous = value;
  }
  assert(first_step < middle_step / 10.0);
}

void testGlideWaitsForTheBarLineAndLandsOnTarget() {
  TempoEngine engine;
  engine.reset(bpm(120.0));
  run(engine, 5);
  engine.requestSampleTempo(bpm(150.0));
  assert(engine.pendingRequest());

  // Until the transport reports a bar line the tempo does not move.
  const std::vector<TickPlan> waiting = run(engine, 40);
  for (const TickPlan& plan : waiting) assert(plan.tempo_q16 == bpm(120.0));
  assert(!engine.gliding());

  crossBarLine(engine);
  assert(!engine.pendingRequest());
  assert(engine.gliding());
  assert(run(engine, 1).back().tempo_q16 == bpm(120.0));  // starts at T0

  // The glide lands exactly on the target at tick N.
  const uint32_t n = TempoEngine::glideTicksFor(bpm(120.0), bpm(150.0));
  const std::vector<TickPlan> glide = run(engine, n);
  assert(glide.back().tempo_q16 == bpm(150.0));
  assert(!engine.gliding());
  assert(run(engine, 10).back().tempo_q16 == bpm(150.0));
}

void testStoppedPlayerTakesTheTempoWithoutGliding() {
  TempoEngine engine;
  engine.reset(bpm(120.0));
  engine.requestSampleTempo(bpm(170.0));
  assert(engine.applyPendingTempoNow());
  assert(!engine.gliding());
  assert(engine.tempo() == bpm(170.0));
  assert(!engine.applyPendingTempoNow());
}

void testGlideDuringGlideRestartsFromCurrentTempo() {
  TempoEngine engine;
  engine.reset(bpm(100.0));
  engine.requestSampleTempo(bpm(150.0));
  crossBarLine(engine);
  run(engine, kTicksPerBar / 2u);
  const uint32_t mid_tempo = engine.tempo();
  assert(mid_tempo > bpm(100.0) && mid_tempo < bpm(150.0));

  engine.requestSampleTempo(bpm(90.0));
  // The new glide begins on the next bar line, from wherever the tempo is now.
  run(engine, 10);
  crossBarLine(engine);
  const uint32_t start = engine.tempo();
  const uint32_t n = TempoEngine::glideTicksFor(start, bpm(90.0));
  // Tick 0 of the glide sits at the starting tempo, tick N lands on target.
  const std::vector<TickPlan> glide = run(engine, n + 1u);
  assert(glide.back().tempo_q16 == bpm(90.0));
}

void testNoGlideWhenTempoMatches() {
  TempoEngine engine;
  engine.reset(bpm(128.0));
  engine.requestSampleTempo(bpm(128.0));
  // The sample still changes on the bar line, but the tempo does not move.
  assert(engine.startPendingGlide());
  const std::vector<TickPlan> plans = run(engine, kTicksPerBar + 10u);
  for (const TickPlan& plan : plans) assert(plan.tempo_q16 == bpm(128.0));
  assert(!engine.gliding());
}

void testKnobTakesOverAndCancelsGlide() {
  TempoEngine engine;
  engine.reset(bpm(120.0));
  engine.requestSampleTempo(bpm(170.0));
  crossBarLine(engine);
  run(engine, 40);
  assert(engine.gliding());

  engine.setKnobTempo(bpm(131.5));
  assert(!engine.gliding());
  const std::vector<TickPlan> plans = run(engine, 20);
  for (const TickPlan& plan : plans) assert(plan.tempo_q16 == bpm(131.5));
}

void testTempoIsClamped() {
  TempoEngine engine;
  engine.reset(bpm(10.0));
  assert(engine.tempo() == bpm(40.0));
  engine.setKnobTempo(bpm(500.0));
  assert(engine.tempo() == bpm(300.0));
}

// The tempo handed to audio for the following tick has to match what that
// tick actually runs at, or interpolation would aim at the wrong value.
void testNextTempoMatchesTheFollowingTick() {
  TempoEngine engine;
  engine.reset(bpm(100.0));
  engine.requestSampleTempo(bpm(150.0));
  crossBarLine(engine);
  const std::vector<TickPlan> plans = run(engine, kTicksPerBar * 4u);
  for (size_t i = 0; i + 1 < plans.size(); ++i) {
    assert(plans[i].next_tempo_q16 == plans[i + 1].tempo_q16);
  }
}

}  // namespace

int main() {
  testTickPeriodCarriesTheFraction();
  testGlideLengthSelection();
  testGlideCurve();
  testGlideWaitsForTheBarLineAndLandsOnTarget();
  testStoppedPlayerTakesTheTempoWithoutGliding();
  testGlideDuringGlideRestartsFromCurrentTempo();
  testNoGlideWhenTempoMatches();
  testKnobTakesOverAndCancelsGlide();
  testTempoIsClamped();
  testNextTempoMatchesTheFollowingTick();
  puts("tempo_engine_test: all tests passed");
  return 0;
}
