#pragma once

#include <stdint.h>

namespace piko {

// Tempo is carried as Q16 BPM so fractional tempos survive glides.
static constexpr uint32_t kTempoQ16One = 65536u;
static constexpr uint32_t kTicksPerQuarter = 24u;   // 24 PPQN
static constexpr uint32_t kTicksPerBar = 96u;       // 4/4
static constexpr uint32_t kGlideShortTicks = 2u * kTicksPerBar;  // 192
static constexpr uint32_t kGlideLongTicks = 4u * kTicksPerBar;   // 384
static constexpr uint32_t kTempoMinQ16 = 40u * kTempoQ16One;
static constexpr uint32_t kTempoMaxQ16 = 300u * kTempoQ16One;

// What one tick looks like to the clock interrupt.
struct TickPlan {
  uint32_t period_us;       // integer microseconds until the following tick
  uint32_t tempo_q16;       // tempo this tick runs at
  uint32_t next_tempo_q16;  // tempo the following tick will run at
  uint32_t tick_index;
  bool bar_start;
  bool sample_swap;  // the pending sample selection takes effect on this tick
};

// Owns the master tempo: the selected sample's tempo, glides between samples
// and the tempo knob. Pure integer/float logic so it can be tested natively.
class TempoEngine {
 public:
  void reset(uint32_t tempo_q16);

  uint32_t tempo() const { return tempo_q16_; }
  uint32_t tickIndex() const { return tick_index_; }
  bool gliding() const { return glide_total_ > 0; }
  bool pendingRequest() const { return pending_request_; }

  // A new sample was selected: it enters, and the glide starts, on the next
  // bar line.
  void requestSampleTempo(uint32_t target_q16);

  // Absolute tempo from the knob. Taking over cancels a running glide.
  void setKnobTempo(uint32_t tempo_q16);

  // Plans the next tick and advances the engine. Called once per tick.
  TickPlan planNextTick();

  // 2 bars unless the per-tick tempo step would exceed the limit, then 4.
  static uint32_t glideTicksFor(uint32_t from_q16, uint32_t to_q16);
  // tempo(k) = T0 * (T1 / T0) ^ smootherstep(k / n)
  static uint32_t glideTempo(uint32_t from_q16, uint32_t to_q16, uint32_t k,
                             uint32_t n);
  // Tick period in microseconds, Q16.
  static uint32_t periodUsQ16(uint32_t tempo_q16);
  static uint32_t clampTempo(uint32_t tempo_q16);

 private:
  uint32_t tempoForTick(uint32_t glide_step) const;

  uint32_t tempo_q16_ = 120u * kTempoQ16One;
  uint32_t tick_index_ = 0;  // index of the last planned tick
  bool first_tick_ = true;

  bool pending_request_ = false;
  uint32_t pending_target_q16_ = 0;

  uint32_t glide_from_q16_ = 0;
  uint32_t glide_to_q16_ = 0;
  uint32_t glide_total_ = 0;  // 0 when no glide is running
  uint32_t glide_step_ = 0;

  // Carries the sub-microsecond remainder of one tick into the next.
  uint32_t period_carry_q16_ = 0;
};

}  // namespace piko
