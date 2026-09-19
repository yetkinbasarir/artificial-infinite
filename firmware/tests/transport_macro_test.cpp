#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <vector>

#include "MacroEngine.h"
#include "Transport.h"

using piko::euclideanHit;
using piko::kMacroOne;
using piko::kNudgeLimitTicks;
using piko::kTicksPerBar;
using piko::kTicksPerQuarter;
using piko::kTicksPerStep;
using piko::MacroEngine;
using piko::MacroMode;
using piko::Transport;
using piko::TransportTick;

namespace {

std::vector<TransportTick> run(Transport& transport, uint32_t ticks) {
  std::vector<TransportTick> out;
  for (uint32_t i = 0; i < ticks; ++i) out.push_back(transport.advanceRawTick());
  return out;
}

uint32_t countSteps(const std::vector<TransportTick>& ticks) {
  uint32_t count = 0;
  for (const TransportTick& tick : ticks) {
    if (tick.step) count++;
  }
  return count;
}

void testStoppedAtBoot() {
  Transport transport;
  transport.reset();
  assert(!transport.playing());
  const std::vector<TransportTick> ticks = run(transport, 200);
  assert(countSteps(ticks) == 0);
  assert(transport.position() == 0);
}

void testStartPlaysFromTheFirstStep() {
  Transport transport;
  transport.reset();
  run(transport, 50);  // the clock runs while stopped
  transport.requestStart();

  const TransportTick first = transport.advanceRawTick();
  assert(first.playing);
  assert(first.step && first.bar && first.period);
  assert(first.position == 0);
  assert(first.step_index == 0);

  // One step every twelve ticks, one bar line every ninety-six.
  const std::vector<TransportTick> bar = run(transport, kTicksPerBar);
  assert(countSteps(bar) == kTicksPerBar / kTicksPerStep);
  uint32_t bars = 0;
  for (const TransportTick& tick : bar) {
    if (tick.bar) bars++;
  }
  assert(bars == 1);
}

void testStopFreezesTheStepsButNotTheClock() {
  Transport transport;
  transport.reset();
  transport.requestStart();
  run(transport, 100);
  const uint32_t position = transport.position();

  transport.requestStop();
  const std::vector<TransportTick> stopped = run(transport, 200);
  assert(countSteps(stopped) == 0);
  assert(transport.position() == position);
  assert(!transport.playing());

  // Starting again returns to the first step and clears the nudge.
  transport.nudge(24);
  transport.requestStart();
  const TransportTick restart = transport.advanceRawTick();
  assert(restart.position == 0);
  assert(restart.step);
  assert(transport.nudgeOffset() == 0);
}

void testPositiveNudgeRunsExtraTicksAtOnce() {
  Transport transport;
  transport.reset();
  transport.requestStart();
  run(transport, 1);          // position 0
  run(transport, 5);          // position 5
  transport.nudge(12);        // one step forward

  const TransportTick tick = transport.advanceRawTick();
  assert(transport.nudgeOffset() == 12);
  assert(tick.position == 18);  // 6 + 12
  // Two step boundaries were crossed; only one trigger comes out.
  assert(tick.step);
  assert(tick.step_index == 1);
}

void testNegativeNudgeSwallowsTicks() {
  Transport transport;
  transport.reset();
  transport.requestStart();
  run(transport, 10);
  const uint32_t position = transport.position();
  transport.nudge(-6);
  assert(transport.nudgeOffset() == -6);

  const std::vector<TransportTick> swallowed = run(transport, 6);
  for (const TransportTick& tick : swallowed) assert(!tick.step);
  assert(transport.position() == position);

  const TransportTick resumed = transport.advanceRawTick();
  assert(resumed.position == position + 1);
}

void testNudgeIsClampedAndClearable() {
  Transport transport;
  transport.reset();
  transport.requestStart();
  run(transport, 1);
  for (uint32_t i = 0; i < 10; ++i) transport.nudge(24);
  assert(transport.nudgeOffset() == kNudgeLimitTicks);
  for (uint32_t i = 0; i < 20; ++i) transport.nudge(-24);
  assert(transport.nudgeOffset() == -kNudgeLimitTicks);

  transport.clearNudge();
  assert(transport.nudgeOffset() == 0);
}

void testNudgeSurvivesAndShiftsThePosition() {
  Transport a;
  Transport b;
  a.reset();
  b.reset();
  a.requestStart();
  b.requestStart();
  run(a, 1);
  run(b, 1);
  b.nudge(12);
  run(a, 48);
  run(b, 48);
  // The nudged player sits exactly one step ahead of the other.
  assert(b.position() == a.position() + 12);
}

void testPeriodBoundaries() {
  Transport transport;
  transport.reset();
  transport.setPeriodTicks(2u * kTicksPerQuarter);  // two beats
  transport.requestStart();
  run(transport, 1);
  const std::vector<TransportTick> ticks = run(transport, 4u * kTicksPerQuarter);
  uint32_t periods = 0;
  for (const TransportTick& tick : ticks) {
    if (tick.period) periods++;
  }
  assert(periods == 2);
}

void testEuclidean() {
  // Four of eight steps: every other one.
  uint32_t hits = 0;
  for (uint32_t i = 0; i < 8; ++i) hits += euclideanHit(i, 4, 8, 0) ? 1 : 0;
  assert(hits == 4);
  // Two of eight, still spread out rather than adjacent.
  assert(euclideanHit(0, 2, 8, 0));
  assert(euclideanHit(4, 2, 8, 0));
  hits = 0;
  for (uint32_t i = 0; i < 8; ++i) hits += euclideanHit(i, 2, 8, 0) ? 1 : 0;
  assert(hits == 2);
}

void testMacroDeadZone() {
  MacroEngine macro;
  macro.setMode(1);
  macro.applyPendingMode();
  macro.setIntensity(0);
  macro.reset();
  assert(!macro.active());
  for (uint32_t i = 0; i < 16; ++i) {
    const piko::MacroStep& step = macro.step(i);
    assert(step.play && step.gate_percent == 100 && !step.jump && !step.reverse && !step.roll);
  }

  // Just inside the dead zone, still nothing.
  macro.setIntensity((kMacroOne * 2u) / 100u);
  assert(!macro.active());
  macro.setIntensity((kMacroOne * 10u) / 100u);
  assert(macro.active());
}

void testMacroModesDoWhatTheySay() {
  MacroEngine macro;

  // Thin: gates shorten and steps drop, never below two per bar.
  macro.setMode(static_cast<uint8_t>(MacroMode::Thin));
  macro.applyPendingMode();
  macro.setIntensity(kMacroOne);
  macro.beginPeriod(1);
  uint32_t playing = 0;
  for (uint32_t i = 0; i < 8; ++i) {
    const piko::MacroStep& step = macro.step(i);
    if (step.play) playing++;
    assert(step.gate_percent == 25);
  }
  assert(playing == 2);

  macro.setIntensity(kMacroOne / 4u);
  macro.beginPeriod(2);
  playing = 0;
  for (uint32_t i = 0; i < 8; ++i) playing += macro.step(i).play ? 1 : 0;
  assert(playing == 8);  // below 0.3 nothing is dropped
  assert(macro.step(0).gate_percent < 100);

  // Shuffle: jumps, but every step still plays.
  macro.setMode(static_cast<uint8_t>(MacroMode::Shuffle));
  macro.applyPendingMode();
  macro.setIntensity(kMacroOne);
  macro.beginPeriod(3);
  uint32_t jumps = 0;
  for (uint32_t i = 0; i < macro.patternSteps(); ++i) {
    assert(macro.step(i).play);
    jumps += macro.step(i).jump ? 1 : 0;
  }
  assert(jumps > 0);

  // Roll: rolls land at the end of a bar.
  macro.setMode(static_cast<uint8_t>(MacroMode::Roll));
  macro.applyPendingMode();
  macro.setIntensity(kMacroOne);
  macro.beginPeriod(4);
  uint32_t rolls = 0;
  for (uint32_t i = 0; i < macro.patternSteps(); ++i) {
    if (macro.step(i).roll) {
      rolls++;
      assert(i % 8 >= 4);  // in the second half of the bar
      assert(macro.step(i).roll_division == 32);
    }
  }
  assert(rolls > 0);

  // Reverse: reversals, and jumps only past the middle of the knob.
  macro.setMode(static_cast<uint8_t>(MacroMode::Reverse));
  macro.applyPendingMode();
  macro.setIntensity(kMacroOne / 3u);
  macro.beginPeriod(5);
  uint32_t reverses = 0;
  uint32_t jumps_low = 0;
  for (uint32_t i = 0; i < macro.patternSteps(); ++i) {
    reverses += macro.step(i).reverse ? 1 : 0;
    jumps_low += macro.step(i).jump ? 1 : 0;
  }
  assert(reverses > 0);
  assert(jumps_low == 0);

  // Abstract: all of it, at the top of the knob.
  macro.setMode(static_cast<uint8_t>(MacroMode::Abstract));
  macro.applyPendingMode();
  macro.setIntensity(kMacroOne);
  macro.beginPeriod(6);
  uint32_t abstract_jumps = 0;
  for (uint32_t i = 0; i < macro.patternSteps(); ++i) {
    abstract_jumps += macro.step(i).jump ? 1 : 0;
  }
  assert(abstract_jumps > 0);
}

void testMacroPeriodLadderAndRepeatability() {
  MacroEngine macro;
  macro.setMode(static_cast<uint8_t>(MacroMode::Thin));
  macro.applyPendingMode();
  macro.setIntensity(0);
  assert(macro.periodTicks() == 8u * kTicksPerBar);
  macro.setIntensity(kMacroOne);
  assert(macro.periodTicks() == 2u * kTicksPerBar);

  macro.setMode(static_cast<uint8_t>(MacroMode::Abstract));
  macro.applyPendingMode();
  macro.setIntensity(kMacroOne);
  assert(macro.periodTicks() == kTicksPerQuarter);

  // The same period draws the same pattern; the next one differs.
  macro.setMode(static_cast<uint8_t>(MacroMode::Shuffle));
  macro.applyPendingMode();
  macro.setIntensity(kMacroOne / 2u);
  macro.beginPeriod(7);
  std::vector<bool> first;
  for (uint32_t i = 0; i < macro.patternSteps(); ++i) first.push_back(macro.step(i).jump);
  macro.beginPeriod(8);
  std::vector<bool> second;
  for (uint32_t i = 0; i < macro.patternSteps(); ++i) second.push_back(macro.step(i).jump);
  macro.beginPeriod(7);
  std::vector<bool> again;
  for (uint32_t i = 0; i < macro.patternSteps(); ++i) again.push_back(macro.step(i).jump);
  assert(first == again);
  assert(first != second);
}

void testMacroModeWaitsForTheBar() {
  MacroEngine macro;
  macro.setMode(static_cast<uint8_t>(MacroMode::Thin));
  macro.applyPendingMode();
  macro.setMode(static_cast<uint8_t>(MacroMode::Roll));
  assert(macro.mode() == static_cast<uint8_t>(MacroMode::Thin));
  assert(macro.pendingMode() == static_cast<uint8_t>(MacroMode::Roll));
  macro.applyPendingMode();
  assert(macro.mode() == static_cast<uint8_t>(MacroMode::Roll));
}

void testPreparedPatternIsUsed() {
  MacroEngine macro;
  macro.setMode(static_cast<uint8_t>(MacroMode::Shuffle));
  macro.applyPendingMode();
  macro.setIntensity(kMacroOne / 2u);
  macro.beginPeriod(10);
  macro.prepareNextPeriod(11);
  assert(macro.takePrepared(11));
  assert(!macro.takePrepared(11));  // only once
}

}  // namespace

int main() {
  testStoppedAtBoot();
  testStartPlaysFromTheFirstStep();
  testStopFreezesTheStepsButNotTheClock();
  testPositiveNudgeRunsExtraTicksAtOnce();
  testNegativeNudgeSwallowsTicks();
  testNudgeIsClampedAndClearable();
  testNudgeSurvivesAndShiftsThePosition();
  testPeriodBoundaries();
  testEuclidean();
  testMacroDeadZone();
  testMacroModesDoWhatTheySay();
  testMacroPeriodLadderAndRepeatability();
  testMacroModeWaitsForTheBar();
  testPreparedPatternIsUsed();
  puts("transport_macro_test: all tests passed");
  return 0;
}
