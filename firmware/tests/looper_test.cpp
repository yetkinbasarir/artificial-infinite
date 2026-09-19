#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <vector>

#include "Looper.h"

using piko::kLoopBeatTicks;
using piko::kLoopMaxTicks;
using piko::kTicksPerStep;
using piko::LoopTrigger;
using piko::Looper;

namespace {

// Runs the loop over `ticks` musical ticks from `from`, collecting triggers.
std::vector<std::pair<uint32_t, LoopTrigger>> run(Looper& looper, uint32_t from,
                                                  uint32_t ticks) {
  std::vector<std::pair<uint32_t, LoopTrigger>> out;
  for (uint32_t i = 0; i < ticks; ++i) {
    const uint32_t position = from + i;
    const LoopTrigger trigger = looper.tick(position);
    if (trigger.trigger) out.push_back({position, trigger});
  }
  return out;
}

Looper makeLooper() {
  Looper looper;
  looper.reset();
  looper.setEnabled(true);
  looper.setVelocity(200);
  return looper;
}

void testRecordsFromTheBeatTheFirstPressFallsIn() {
  Looper looper = makeLooper();
  // A press five ticks into the beat that starts at 96.
  looper.pressButton(2, 5, 101, 1000);
  assert(looper.recording());
  looper.releaseButton(2, 107, 1050);

  // Close two beats later, on a beat line.
  looper.closeLoop(149, 2000);
  assert(!looper.recording());
  assert(looper.defined());
  // 101 - 96 = 5 recorded, closing at 149 - 96 = 53 rounds to two beats.
  assert(looper.length() == 2u * kLoopBeatTicks);

  // The event keeps its place inside the beat: five ticks in.
  const std::vector<std::pair<uint32_t, LoopTrigger>> hits = run(looper, 149, 96);
  assert(!hits.empty());
  assert((hits[0].first - 149) % looper.length() == 5u);
  assert(hits[0].second.slice == 5);
  assert(hits[0].second.velocity == 200);
}

void testClosingRoundsToAtLeastOneBeat() {
  Looper looper = makeLooper();
  looper.pressButton(0, 0, 0, 0);
  looper.releaseButton(0, 6, 60);
  // Closed well after the press, so the press itself is kept.
  looper.closeLoop(3, 300);
  assert(looper.length() == kLoopBeatTicks);
  assert(looper.eventCount() == 1u);
}

// A closing gesture right after the only press leaves nothing behind.
void testGestureRightAfterTheOnlyPressLeavesNoLoop() {
  Looper looper = makeLooper();
  looper.pressButton(0, 0, 0, 0);
  looper.closeLoop(3, 10);
  assert(looper.eventCount() == 0u);
  assert(!looper.defined());
}

void testTwoPressesInsideFiftyMillisecondsAreOneGesture() {
  Looper looper = makeLooper();
  looper.pressButton(1, 1, 0, 0);
  looper.pressButton(3, 3, 24, 100);  // a real note, 100 ms later
  looper.releaseButton(1, 6, 20);
  looper.releaseButton(3, 30, 120);
  // The closing press lands within 50 ms of the last press.
  looper.pressButton(5, 5, 48, 500);
  looper.closeLoop(48, 520);

  // The gesture press is gone; the two played notes remain.
  assert(looper.eventCount() == 2u);
}

// A button still down when the loop closes is part of the gesture, so its
// press never enters the loop.
void testHeldButtonsAtCloseAreNotRecorded() {
  Looper looper = makeLooper();
  looper.pressButton(0, 1, 0, 0);
  looper.releaseButton(0, 6, 100);      // a note that stays
  looper.pressButton(2, 2, 24, 400);    // still held when the loop closes
  looper.closeLoop(48, 800);            // well past the 50 ms window

  assert(looper.eventCount() == 1u);
  // Its release afterwards changes nothing.
  looper.releaseButton(2, 60, 900);
  assert(looper.eventCount() == 1u);

  const std::vector<std::pair<uint32_t, LoopTrigger>> hits =
      run(looper, 48, 2u * kLoopBeatTicks);
  for (const auto& hit : hits) assert(hit.second.slice == 1);
}

void testAutoClosesAtEightBars() {
  Looper looper = makeLooper();
  looper.pressButton(0, 0, 0, 0);
  looper.releaseButton(0, 6, 10);
  run(looper, 0, kLoopMaxTicks + 1u);
  assert(!looper.recording());
  assert(looper.length() == kLoopMaxTicks);
}

void testOverdubAndLastPressWins() {
  Looper looper = makeLooper();
  looper.pressButton(0, 1, 0, 0);
  looper.releaseButton(0, 6, 10);
  looper.closeLoop(kLoopBeatTicks, 100);
  assert(looper.length() == kLoopBeatTicks);

  // Overdub another event at the same place in the loop.
  const uint32_t base = kLoopBeatTicks;
  looper.pressButton(4, 7, base, 200);
  looper.releaseButton(4, base + 6u, 210);

  const std::vector<std::pair<uint32_t, LoopTrigger>> hits =
      run(looper, base + kLoopBeatTicks, kLoopBeatTicks);
  assert(hits.size() == 1u);
  // Both sit on tick 0 of the loop; the later press is the one heard.
  assert(hits[0].second.slice == 7);
}

void testEraseZoneRemovesOneButton() {
  Looper looper = makeLooper();
  looper.pressButton(0, 1, 0, 0);
  looper.releaseButton(0, 6, 10);
  looper.pressButton(1, 2, 12, 20);
  looper.releaseButton(1, 18, 30);
  looper.closeLoop(kLoopBeatTicks, 100);
  assert(looper.eventCount() == 2u);

  looper.setErasing(true);
  looper.pressButton(0, 1, kLoopBeatTicks * 2u, 200);  // erases button 0
  assert(looper.eventCount() == 1u);
  // Nothing new was recorded while erasing.
  looper.setErasing(false);
  assert(looper.eventCount() == 1u);
}

void testDisabledLoopFadesAwayAndFrees() {
  Looper looper = makeLooper();
  looper.pressButton(0, 1, 0, 0);
  looper.releaseButton(0, 6, 10);
  looper.closeLoop(kLoopBeatTicks, 100);
  const uint32_t start = kLoopBeatTicks;
  assert(looper.defined());

  looper.setEnabled(false);
  uint8_t previous = 200;
  for (uint32_t pass = 0; pass < 12 && looper.defined(); ++pass) {
    const std::vector<std::pair<uint32_t, LoopTrigger>> hits =
        run(looper, start + pass * kLoopBeatTicks, kLoopBeatTicks);
    if (!hits.empty()) {
      // Each pass is quieter than the one before.
      assert(hits[0].second.velocity <= previous);
      previous = hits[0].second.velocity;
    }
  }
  assert(!looper.defined());
  assert(looper.eventCount() == 0u);

  // An undefined loop takes a new recording again.
  looper.setEnabled(true);
  looper.pressButton(2, 2, 1000, 900);
  assert(looper.recording());
}

void testLoopHoldsWhileEnabled() {
  Looper looper = makeLooper();
  looper.pressButton(0, 3, 0, 0);
  looper.releaseButton(0, 6, 10);
  looper.closeLoop(kLoopBeatTicks, 100);

  uint32_t hits = 0;
  for (uint32_t pass = 0; pass < 20; ++pass) {
    hits += run(looper, kLoopBeatTicks + pass * kLoopBeatTicks, kLoopBeatTicks).size();
  }
  assert(hits == 20u);  // twenty passes, one hit each, no fading
  assert(looper.defined());
}

void testGateLengthComesFromTheHold() {
  Looper looper = makeLooper();
  looper.pressButton(0, 1, 0, 0);
  looper.releaseButton(0, 30, 50);  // held for 30 ticks
  looper.closeLoop(2u * kLoopBeatTicks, 100);
  const std::vector<std::pair<uint32_t, LoopTrigger>> hits =
      run(looper, 2u * kLoopBeatTicks, 2u * kLoopBeatTicks);
  assert(hits.size() == 1u);
  assert(hits[0].second.ticks == 30u);
}

void testTransportStartRestartsTheLoopWithoutLosingIt() {
  Looper looper = makeLooper();
  looper.pressButton(0, 1, 0, 0);
  looper.releaseButton(0, 6, 10);
  looper.closeLoop(kLoopBeatTicks, 100);
  const uint32_t events = looper.eventCount();

  looper.onTransportStart(0);
  assert(looper.eventCount() == events);
  assert(looper.defined());
  // The loop plays from its own beginning together with the first step.
  const std::vector<std::pair<uint32_t, LoopTrigger>> hits = run(looper, 0, kLoopBeatTicks);
  assert(hits.size() == 1u);
  assert(hits[0].first == 0u);
}

void testErasingEverythingLeavesTheLoopUndefined() {
  Looper looper = makeLooper();
  looper.pressButton(0, 1, 0, 0);
  looper.releaseButton(0, 6, 10);
  looper.closeLoop(kLoopBeatTicks, 100);
  looper.setErasing(true);
  looper.pressButton(0, 1, kLoopBeatTicks, 200);
  assert(looper.eventCount() == 0u);
  assert(!looper.defined());
}

}  // namespace

int main() {
  testRecordsFromTheBeatTheFirstPressFallsIn();
  testClosingRoundsToAtLeastOneBeat();
  testGestureRightAfterTheOnlyPressLeavesNoLoop();
  testTwoPressesInsideFiftyMillisecondsAreOneGesture();
  testHeldButtonsAtCloseAreNotRecorded();
  testAutoClosesAtEightBars();
  testOverdubAndLastPressWins();
  testEraseZoneRemovesOneButton();
  testDisabledLoopFadesAwayAndFrees();
  testLoopHoldsWhileEnabled();
  testGateLengthComesFromTheHold();
  testTransportStartRestartsTheLoopWithoutLosingIt();
  testErasingEverythingLeavesTheLoopUndefined();
  puts("looper_test: all tests passed");
  return 0;
}
