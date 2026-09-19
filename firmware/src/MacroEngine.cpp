#include "MacroEngine.h"

namespace piko {
namespace {

constexpr uint32_t kBar = kTicksPerBar;
constexpr uint32_t kBeat = kTicksPerQuarter;
constexpr uint32_t kStepsPerBar = kTicksPerBar / kTicksPerStep;

// Period ladders: more intensity, shorter period.
constexpr uint32_t kThinPeriods[] = {8u * kBar, 4u * kBar, 2u * kBar};
constexpr uint32_t kShufflePeriods[] = {4u * kBar, 2u * kBar, kBar, 2u * kBeat, kBeat};
constexpr uint32_t kRollPeriods[] = {4u * kBar, 2u * kBar, kBar};
constexpr uint32_t kReversePeriods[] = {2u * kBar, kBar, 2u * kBeat, kBeat};
constexpr uint32_t kAbstractPeriods[] = {8u * kBar, 4u * kBar, 2u * kBar, kBar, 2u * kBeat, kBeat};

struct Ladder {
  const uint32_t* periods;
  uint32_t count;
};

Ladder ladderFor(uint8_t mode) {
  switch (static_cast<MacroMode>(mode)) {
    case MacroMode::Shuffle:
      return {kShufflePeriods, 5};
    case MacroMode::Roll:
      return {kRollPeriods, 3};
    case MacroMode::Reverse:
      return {kReversePeriods, 4};
    case MacroMode::Abstract:
      return {kAbstractPeriods, 6};
    case MacroMode::Thin:
    default:
      return {kThinPeriods, 3};
  }
}

uint32_t xorshift(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

// Chance in 0..65535.
bool chance(uint32_t& state, uint32_t probability) {
  return (xorshift(state) & 0xffffu) < probability;
}

uint32_t scaled(uint16_t intensity, uint32_t percent_x100) {
  return (static_cast<uint32_t>(intensity) * percent_x100) / 10000u;
}

// Maps d in [from, to] onto 0..65535, clamped outside.
uint32_t ramp(uint16_t intensity, uint32_t from, uint32_t to) {
  if (intensity <= from) return 0;
  if (intensity >= to || to <= from) return kMacroOne;
  return ((static_cast<uint32_t>(intensity) - from) * kMacroOne) / (to - from);
}

uint8_t gateFromIntensity(uint16_t intensity) {
  // 100 % down to 25 % over the first half of the knob.
  const uint32_t t = ramp(intensity, 0, kMacroOne / 2u);
  return static_cast<uint8_t>(100u - (75u * t) / kMacroOne);
}

uint32_t keepCount(uint16_t intensity, uint32_t steps_per_bar) {
  // Thinning starts at 0.3 and takes the bar down to two steps.
  const uint32_t t = ramp(intensity, (kMacroOne * 3u) / 10u, kMacroOne);
  const uint32_t span = steps_per_bar > 2u ? steps_per_bar - 2u : 0u;
  const uint32_t dropped = (span * t) / kMacroOne;
  const uint32_t keep = steps_per_bar - dropped;
  return keep < 2u ? 2u : keep;
}

}  // namespace

bool euclideanHit(uint32_t index, uint32_t pulses, uint32_t steps, uint32_t rotation) {
  if (steps == 0 || pulses == 0) return false;
  if (pulses >= steps) return true;
  const uint32_t position = (index + rotation) % steps;
  return ((position * pulses) % steps) < pulses;
}

void MacroEngine::reset() {
  pattern_ = Pattern{};
  prepared_ = Pattern{};
  pattern_steps_ = 8;
  generate(pattern_, 0);
  pattern_steps_ = pattern_.length;
}

void MacroEngine::setIntensity(uint16_t intensity) { intensity_ = intensity; }

void MacroEngine::setMode(uint8_t mode) {
  if (mode < 1 || mode > kMacroModes) return;
  pending_mode_ = mode;
}

void MacroEngine::applyPendingMode() { mode_ = pending_mode_; }

uint32_t MacroEngine::periodTicks() const {
  const Ladder ladder = ladderFor(mode_);
  uint32_t index = (static_cast<uint32_t>(intensity_) * ladder.count) / (kMacroOne + 1u);
  if (index >= ladder.count) index = ladder.count - 1u;
  return ladder.periods[index];
}

void MacroEngine::beginPeriod(uint32_t period_index) {
  if (takePrepared(period_index)) return;
  generate(pattern_, period_index);
  pattern_steps_ = pattern_.length;
}

void MacroEngine::prepareNextPeriod(uint32_t period_index) {
  if (prepared_.valid && prepared_.period_index == period_index) return;
  generate(prepared_, period_index);
}

bool MacroEngine::takePrepared(uint32_t period_index) {
  if (!prepared_.valid || prepared_.period_index != period_index) return false;
  pattern_ = prepared_;
  pattern_steps_ = pattern_.length;
  prepared_.valid = false;
  return true;
}

const MacroStep& MacroEngine::step(uint32_t step_index) const {
  if (!active() || pattern_.length == 0) return neutral_;
  return pattern_.steps[step_index % pattern_.length];
}

void MacroEngine::generate(Pattern& pattern, uint32_t period_index) const {
  const uint32_t period = periodTicks();
  uint32_t steps = period / kTicksPerStep;
  if (steps < 1) steps = 1;
  if (steps > kMacroMaxSteps) steps = kMacroMaxSteps;
  pattern.length = steps;
  pattern.period_index = period_index;
  // Marked valid only once it is fully drawn, so the tick interrupt can never
  // pick up a half-written pattern.
  pattern.valid = false;

  // Same period, same pattern; neighbouring periods differ.
  uint32_t state = (period_index * 2654435761u) ^ (static_cast<uint32_t>(mode_) * 40503u);
  if (state == 0) state = 0x1234567u;
  xorshift(state);

  const uint16_t d = intensity_;
  const uint32_t rotation = xorshift(state) % kStepsPerBar;

  for (uint32_t i = 0; i < steps; ++i) {
    MacroStep step;
    if (!active()) {
      pattern.steps[i] = step;
      continue;
    }

    switch (static_cast<MacroMode>(mode_)) {
      case MacroMode::Thin: {
        step.gate_percent = gateFromIntensity(d);
        const uint32_t keep = keepCount(d, kStepsPerBar);
        step.play = euclideanHit(i % kStepsPerBar, keep, kStepsPerBar, rotation);
        break;
      }
      case MacroMode::Shuffle: {
        // Three quarters of the steps at most, and nothing is dropped.
        if (chance(state, scaled(d, 7500))) {
          step.jump = true;
          step.jump_slice = static_cast<uint8_t>(xorshift(state) & 0x0fu);
        }
        break;
      }
      case MacroMode::Roll: {
        const uint32_t every_bars = d < kMacroOne / 3u ? 4u : (d < (2u * kMacroOne) / 3u ? 2u : 1u);
        // Half a beat at the bottom of the knob, two beats at the top.
        const uint32_t roll_steps = 1u + (3u * ramp(d, 0, kMacroOne)) / kMacroOne;
        const uint32_t bar = i / kStepsPerBar;
        const uint32_t in_bar = i % kStepsPerBar;
        const bool rolling_bar = ((bar + 1u) % every_bars) == 0u;
        if (rolling_bar && in_bar + roll_steps >= kStepsPerBar) {
          step.roll = true;
          step.roll_division = d < kMacroOne / 2u ? 16u : 32u;
        }
        break;
      }
      case MacroMode::Reverse: {
        if (chance(state, scaled(d, 6000))) step.reverse = true;
        if (d > kMacroOne / 2u) {
          const uint16_t extra = static_cast<uint16_t>(d - kMacroOne / 2u);
          if (chance(state, scaled(extra, 6000))) {
            step.jump = true;
            step.jump_slice = static_cast<uint8_t>(xorshift(state) & 0x0fu);
          }
        }
        break;
      }
      case MacroMode::Abstract:
      default: {
        // One chain: gates and thinning first, then jumps, then rolls.
        step.gate_percent = gateFromIntensity(d);
        const uint32_t keep = keepCount(d, kStepsPerBar);
        step.play = euclideanHit(i % kStepsPerBar, keep, kStepsPerBar, rotation);
        if (d > kMacroOne / 2u) {
          const uint32_t jump_chance = scaled(ramp(d, kMacroOne / 2u, kMacroOne), 7500);
          if (chance(state, jump_chance)) {
            step.jump = true;
            step.jump_slice = static_cast<uint8_t>(xorshift(state) & 0x0fu);
          }
        }
        if (d > (3u * kMacroOne) / 4u) {
          const uint32_t roll_chance = scaled(ramp(d, (3u * kMacroOne) / 4u, kMacroOne), 3000);
          if (step.play && chance(state, roll_chance)) {
            step.roll = true;
            step.roll_division = 32u;
            step.gate_percent = 25u;
          }
        }
        break;
      }
    }
    pattern.steps[i] = step;
  }
  pattern.valid = true;
}

}  // namespace piko
