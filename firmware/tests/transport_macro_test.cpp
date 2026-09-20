#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <vector>

#include "MacroEngine.h"
#include "Transport.h"

using piko::euclideanHit;
using piko::kMacroDeadZone;
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

// A bank write drops the offset instead of playing it back out.
void testResetNudgeDropsWhatIsPending() {
  Transport a;
  Transport b;
  a.reset();
  b.reset();
  a.requestStart();
  b.requestStart();
  run(a, 24);
  run(b, 24);

  a.nudge(-12);   // twelve ticks still to be swallowed
  a.resetNudge();
  assert(a.nudgeOffset() == 0);
  run(a, 24);
  run(b, 24);
  // Nothing was swallowed and nothing is owed: the two stay together.
  assert(a.position() == b.position());

  a.nudge(12);    // twelve ticks still to be run out
  a.resetNudge();
  assert(a.nudgeOffset() == 0);
  run(a, 24);
  run(b, 24);
  assert(a.position() == b.position());
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
    const piko::MacroStep step = macro.resolveStep(i);
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
  macro.beginBar();
  uint32_t playing = 0;
  for (uint32_t i = 0; i < 8; ++i) {
    const piko::MacroStep step = macro.resolveStep(i);
    if (step.play) playing++;
    assert(step.gate_percent == 25);
  }
  assert(playing == 2);

  // Shuffle: jumps, but every step still plays.
  macro.setMode(static_cast<uint8_t>(MacroMode::Shuffle));
  macro.applyPendingMode();
  macro.setIntensity(kMacroOne);
  macro.beginPeriod(3);
  macro.beginBar();
  uint32_t jumps = 0;
  for (uint32_t i = 0; i < macro.patternSteps(); ++i) {
    assert(macro.resolveStep(i).play);
    jumps += macro.resolveStep(i).jump ? 1 : 0;
  }
  assert(jumps > 0);

  // Roll: rolls land at the end of a bar.
  macro.setMode(static_cast<uint8_t>(MacroMode::Roll));
  macro.applyPendingMode();
  macro.setIntensity(kMacroOne);
  macro.beginPeriod(4);
  macro.beginBar();
  uint32_t rolls = 0;
  for (uint32_t i = 0; i < macro.patternSteps(); ++i) {
    if (macro.resolveStep(i).roll) {
      rolls++;
      assert(i % 8 >= 4);  // in the second half of the bar
      assert(macro.resolveStep(i).roll_division == 32);
    }
  }
  assert(rolls > 0);

  // Reverse: reversals, and jumps only past the middle of the knob.
  macro.setMode(static_cast<uint8_t>(MacroMode::Reverse));
  macro.applyPendingMode();
  macro.setIntensity(kMacroOne / 3u);
  macro.beginPeriod(5);
  macro.beginBar();
  uint32_t reverses = 0;
  uint32_t jumps_low = 0;
  for (uint32_t i = 0; i < macro.patternSteps(); ++i) {
    reverses += macro.resolveStep(i).reverse ? 1 : 0;
    jumps_low += macro.resolveStep(i).jump ? 1 : 0;
  }
  assert(reverses > 0);
  assert(jumps_low == 0);

  // Abstract: all of it, at the top of the knob.
  macro.setMode(static_cast<uint8_t>(MacroMode::Abstract));
  macro.applyPendingMode();
  macro.setIntensity(kMacroOne);
  macro.beginPeriod(6);
  macro.beginBar();
  uint32_t abstract_jumps = 0;
  for (uint32_t i = 0; i < macro.patternSteps(); ++i) {
    abstract_jumps += macro.resolveStep(i).jump ? 1 : 0;
  }
  assert(abstract_jumps > 0);
}

// The euclidean count is taken from the intensity at each bar line.
void testEuclideanCountFollowsTheBar() {
  MacroEngine macro;
  macro.setMode(static_cast<uint8_t>(MacroMode::Thin));
  macro.applyPendingMode();
  macro.setIntensity(kMacroOne);
  macro.beginPeriod(1);
  macro.beginBar();
  uint32_t playing = 0;
  for (uint32_t i = 0; i < 8; ++i) playing += macro.resolveStep(i).play ? 1 : 0;
  assert(playing == 2);

  // Turned down, but the bar has not turned over yet.
  macro.setIntensity(kMacroOne / 4u);
  playing = 0;
  for (uint32_t i = 0; i < 8; ++i) playing += macro.resolveStep(i).play ? 1 : 0;
  assert(playing == 2);

  macro.beginBar();
  playing = 0;
  for (uint32_t i = 0; i < 8; ++i) playing += macro.resolveStep(i).play ? 1 : 0;
  assert(playing == 8);
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

  // The same period draws the same dice; the next one differs.
  macro.setMode(static_cast<uint8_t>(MacroMode::Shuffle));
  macro.applyPendingMode();
  macro.setIntensity(kMacroOne / 2u);
  macro.beginPeriod(7);
  macro.beginBar();
  std::vector<bool> first;
  for (uint32_t i = 0; i < macro.patternSteps(); ++i) first.push_back(macro.resolveStep(i).jump);
  macro.beginPeriod(8);
  std::vector<bool> second;
  for (uint32_t i = 0; i < macro.patternSteps(); ++i) second.push_back(macro.resolveStep(i).jump);
  macro.beginPeriod(7);
  std::vector<bool> again;
  for (uint32_t i = 0; i < macro.patternSteps(); ++i) again.push_back(macro.resolveStep(i).jump);
  assert(first == again);
  assert(first != second);
}

void testMacroModeWaitsForTheBar() {
  MacroEngine macro;  // starts on mode 1
  macro.setMode(static_cast<uint8_t>(MacroMode::Roll));
  assert(macro.mode() == static_cast<uint8_t>(MacroMode::Thin));
  assert(macro.pendingMode() == static_cast<uint8_t>(MacroMode::Roll));
  assert(macro.applyPendingMode());  // the bar line takes it
  assert(macro.mode() == static_cast<uint8_t>(MacroMode::Roll));
  assert(!macro.applyPendingMode());  // and only once
}

// Turning the intensity knob is heard on the next step, not the next period.
void testIntensityIsReadAtTheStep() {
  MacroEngine macro;
  macro.setMode(static_cast<uint8_t>(MacroMode::Shuffle));
  macro.applyPendingMode();
  macro.setIntensity(kMacroOne);
  macro.beginPeriod(3);
  macro.beginBar();
  uint32_t jumps_high = 0;
  for (uint32_t i = 0; i < macro.patternSteps(); ++i) {
    jumps_high += macro.resolveStep(i).jump ? 1 : 0;
  }
  assert(jumps_high > 0);

  // Same period, same dice: only the intensity moved.
  macro.setIntensity(kMacroDeadZone + 1u);
  uint32_t jumps_low = 0;
  for (uint32_t i = 0; i < macro.patternSteps(); ++i) {
    jumps_low += macro.resolveStep(i).jump ? 1 : 0;
  }
  assert(jumps_low < jumps_high);

  // Back to zero and the macro is out of the way at once.
  macro.setIntensity(0);
  for (uint32_t i = 0; i < macro.patternSteps(); ++i) {
    const piko::MacroStep step = macro.resolveStep(i);
    assert(step.play && !step.jump && step.gate_percent == 100);
  }
}


}  // namespace

int main() {
  testStoppedAtBoot();
  testStartPlaysFromTheFirstStep();
  testStopFreezesTheStepsButNotTheClock();
  testPositiveNudgeRunsExtraTicksAtOnce();
  testNegativeNudgeSwallowsTicks();
  testNudgeIsClampedAndClearable();
  testResetNudgeDropsWhatIsPending();
  testNudgeSurvivesAndShiftsThePosition();
  testPeriodBoundaries();
  testEuclidean();
  testMacroDeadZone();
  testMacroModesDoWhatTheySay();
  testEuclideanCountFollowsTheBar();
  testMacroPeriodLadderAndRepeatability();
  testMacroModeWaitsForTheBar();
  testIntensityIsReadAtTheStep();
  puts("transport_macro_test: all tests passed");
  return 0;
}
