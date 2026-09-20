#pragma once

#include <stdint.h>

#include "TempoEngine.h"

namespace piko {

// Musical position of the player, which is the clock tick count plus the nudge
// offset. The clock itself never stops or shifts: only this position does.
static constexpr int32_t kNudgeLimitTicks = 96;

struct TransportTick {
  bool step = false;      // a step boundary was crossed
  bool bar = false;       // a bar line was crossed
  bool period = false;    // a macro variation period started
  bool playing = false;
  uint32_t advanced = 0;  // musical ticks covered by this clock tick
  uint32_t position = 0;  // musical tick
  uint32_t step_index = 0;
  uint32_t bar_index = 0;
  uint32_t period_index = 0;
};

class Transport {
 public:
  void reset();

  // Macro variation period, in ticks. Takes effect on the next period.
  void setPeriodTicks(uint32_t ticks);
  uint32_t periodTicks() const { return period_ticks_; }

  // Start plays from the first step on the next tick; stop leaves the clock
  // running and the player silent.
  void requestStart();
  void requestStop();
  void requestToggle();
  bool playing() const { return playing_; }

  // Shifts the player by whole ticks. A positive nudge runs those ticks now, a
  // negative one skips the ticks to come.
  void nudge(int32_t ticks);
  void clearNudge();
  // Drops the offset and anything pending with it, without playing it out.
  void resetNudge();
  int32_t nudgeOffset() const { return nudge_offset_; }

  // Called once per clock tick.
  TransportTick advanceRawTick();

  uint32_t position() const { return position_; }
  uint32_t stepIndex() const { return position_ / kTicksPerStep; }

 private:
  void startNow(TransportTick& tick);

  bool playing_ = false;
  bool start_requested_ = false;
  bool stop_requested_ = false;
  bool first_tick_ = true;

  uint32_t position_ = 0;
  uint32_t period_ticks_ = 8u * kTicksPerBar;
  int32_t nudge_offset_ = 0;
  uint32_t extra_ticks_ = 0;  // positive nudge waiting to be played out
  uint32_t skip_ticks_ = 0;   // negative nudge waiting to swallow ticks
};

}  // namespace piko
