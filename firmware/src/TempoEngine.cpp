#include "TempoEngine.h"

#include <math.h>

namespace piko {
namespace {

// Largest slope of smootherstep, 15/8.
constexpr double kSmootherstepMaxSlope = 1.875;
// Largest relative tempo step allowed per tick before the glide is stretched.
constexpr double kMaxRelativeStepPerTick = 0.0025;

}  // namespace

uint32_t TempoEngine::clampTempo(uint32_t tempo_q16) {
  if (tempo_q16 < kTempoMinQ16) return kTempoMinQ16;
  if (tempo_q16 > kTempoMaxQ16) return kTempoMaxQ16;
  return tempo_q16;
}

void TempoEngine::reset(uint32_t tempo_q16) {
  tempo_q16_ = clampTempo(tempo_q16);
  tick_index_ = 0;
  first_tick_ = true;
  pending_request_ = false;
  pending_target_q16_ = 0;
  glide_total_ = 0;
  glide_step_ = 0;
  period_carry_q16_ = 0;
}

uint32_t TempoEngine::periodUsQ16(uint32_t tempo_q16) {
  const uint32_t tempo = clampTempo(tempo_q16);
  // 60e6 us per minute / 24 ticks per quarter = 2.5e6 us per BPM unit.
  return static_cast<uint32_t>((2500000ull << 32u) / tempo);
}

uint32_t TempoEngine::glideTicksFor(uint32_t from_q16, uint32_t to_q16) {
  if (from_q16 == 0 || to_q16 == 0 || from_q16 == to_q16) {
    return kGlideShortTicks;
  }
  const double ratio = static_cast<double>(to_q16) / static_cast<double>(from_q16);
  const double span = fabs(log(ratio));
  const double step = span * kSmootherstepMaxSlope / kGlideShortTicks;
  // Four bars is the longest glide; past that limit it simply stays there.
  return step <= kMaxRelativeStepPerTick ? kGlideShortTicks : kGlideLongTicks;
}

uint32_t TempoEngine::glideTempo(uint32_t from_q16, uint32_t to_q16, uint32_t k,
                                 uint32_t n) {
  if (n == 0 || k >= n) return to_q16;
  const double t = static_cast<double>(k) / static_cast<double>(n);
  // smootherstep: 6t^5 - 15t^4 + 10t^3
  const double s = t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
  const double from_bpm = static_cast<double>(from_q16);
  const double to_bpm = static_cast<double>(to_q16);
  const double value = from_bpm * pow(to_bpm / from_bpm, s);
  return clampTempo(static_cast<uint32_t>(value + 0.5));
}

void TempoEngine::requestSampleTempo(uint32_t target_q16) {
  pending_request_ = true;
  pending_target_q16_ = clampTempo(target_q16);
}

void TempoEngine::setKnobTempo(uint32_t tempo_q16) {
  // The knob wins: a glide in flight is abandoned where it stands.
  glide_total_ = 0;
  glide_step_ = 0;
  tempo_q16_ = clampTempo(tempo_q16);
}

uint32_t TempoEngine::tempoForTick(uint32_t glide_step) const {
  if (glide_total_ == 0) return tempo_q16_;
  return glideTempo(glide_from_q16_, glide_to_q16_, glide_step, glide_total_);
}

TickPlan TempoEngine::planNextTick() {
  if (first_tick_) {
    first_tick_ = false;
  } else {
    ++tick_index_;
  }

  const bool bar_start = (tick_index_ % kTicksPerBar) == 0;
  bool sample_swap = false;
  if (bar_start && pending_request_) {
    sample_swap = true;
    pending_request_ = false;
    if (pending_target_q16_ == tempo_q16_) {
      // Same tempo as the sample already playing: nothing to glide.
      glide_total_ = 0;
      glide_step_ = 0;
    } else {
      glide_from_q16_ = tempo_q16_;
      glide_to_q16_ = pending_target_q16_;
      glide_total_ = glideTicksFor(glide_from_q16_, glide_to_q16_);
      glide_step_ = 0;
    }
  }

  if (glide_total_ > 0) {
    tempo_q16_ = tempoForTick(glide_step_);
  }

  TickPlan plan{};
  plan.tick_index = tick_index_;
  plan.bar_start = bar_start;
  plan.sample_swap = sample_swap;
  plan.tempo_q16 = tempo_q16_;

  // The tempo the following tick will use, so audio can interpolate towards it.
  if (glide_total_ > 0) {
    const uint32_t next_step = glide_step_ + 1u;
    plan.next_tempo_q16 = next_step >= glide_total_
                              ? glide_to_q16_
                              : tempoForTick(next_step);
  } else {
    plan.next_tempo_q16 = tempo_q16_;
  }

  period_carry_q16_ += periodUsQ16(tempo_q16_);
  plan.period_us = period_carry_q16_ >> 16u;
  period_carry_q16_ &= 0xffffu;

  if (glide_total_ > 0) {
    ++glide_step_;
    if (glide_step_ >= glide_total_) {
      // The last tick of the glide lands exactly on the target tempo.
      tempo_q16_ = glide_to_q16_;
      glide_total_ = 0;
      glide_step_ = 0;
    }
  }

  return plan;
}

}  // namespace piko
