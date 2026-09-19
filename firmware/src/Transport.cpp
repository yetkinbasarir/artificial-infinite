#include "Transport.h"

namespace piko {

void Transport::reset() {
  playing_ = false;
  start_requested_ = false;
  stop_requested_ = false;
  first_tick_ = true;
  position_ = 0;
  nudge_offset_ = 0;
  extra_ticks_ = 0;
  skip_ticks_ = 0;
}

void Transport::setPeriodTicks(uint32_t ticks) {
  period_ticks_ = ticks == 0 ? kTicksPerBar : ticks;
}

void Transport::requestStart() {
  start_requested_ = true;
  stop_requested_ = false;
}

void Transport::requestStop() {
  stop_requested_ = true;
  start_requested_ = false;
}

void Transport::requestToggle() {
  if (playing_ && !stop_requested_) {
    requestStop();
  } else {
    requestStart();
  }
}

void Transport::nudge(int32_t ticks) {
  if (ticks == 0) return;
  int32_t target = nudge_offset_ + ticks;
  if (target > kNudgeLimitTicks) target = kNudgeLimitTicks;
  if (target < -kNudgeLimitTicks) target = -kNudgeLimitTicks;
  const int32_t applied = target - nudge_offset_;
  if (applied == 0) return;
  nudge_offset_ = target;
  if (applied > 0) {
    extra_ticks_ += static_cast<uint32_t>(applied);
  } else {
    skip_ticks_ += static_cast<uint32_t>(-applied);
  }
}

void Transport::clearNudge() { nudge(-nudge_offset_); }

void Transport::startNow(TransportTick& tick) {
  playing_ = true;
  start_requested_ = false;
  position_ = 0;
  nudge_offset_ = 0;
  extra_ticks_ = 0;
  skip_ticks_ = 0;
  tick.step = true;
  tick.bar = true;
  tick.period = true;
}

TransportTick Transport::advanceRawTick() {
  TransportTick tick;

  if (stop_requested_) {
    stop_requested_ = false;
    playing_ = false;
  }

  if (start_requested_) {
    startNow(tick);
    tick.playing = true;
    tick.advanced = 1;
    tick.position = position_;
    tick.step_index = position_ / kTicksPerStep;
    tick.bar_index = position_ / kTicksPerBar;
    tick.period_index = period_ticks_ > 0 ? position_ / period_ticks_ : 0;
    first_tick_ = false;
    return tick;
  }

  tick.playing = playing_;
  if (!playing_) {
    tick.position = position_;
    tick.step_index = position_ / kTicksPerStep;
    return tick;
  }

  if (skip_ticks_ > 0) {
    // A backwards nudge swallows the ticks to come.
    --skip_ticks_;
    tick.position = position_;
    tick.step_index = position_ / kTicksPerStep;
    return tick;
  }

  uint32_t advance = 1u + extra_ticks_;
  extra_ticks_ = 0;
  if (first_tick_) {
    // The very first tick after a reset sits on position 0 rather than 1.
    first_tick_ = false;
    advance -= 1u;
    tick.step = true;
    tick.bar = true;
    tick.period = true;
  }

  tick.advanced = advance;
  for (uint32_t i = 0; i < advance; ++i) {
    ++position_;
    // Only the last boundary of a burst triggers, so these just accumulate.
    if (position_ % kTicksPerStep == 0) tick.step = true;
    if (position_ % kTicksPerBar == 0) tick.bar = true;
    if (period_ticks_ > 0 && position_ % period_ticks_ == 0) tick.period = true;
  }

  tick.position = position_;
  tick.step_index = position_ / kTicksPerStep;
  tick.bar_index = position_ / kTicksPerBar;
  tick.period_index = period_ticks_ > 0 ? position_ / period_ticks_ : 0;
  return tick;
}

}  // namespace piko
