#include "ClockSync.h"

#include <limits.h>

namespace piko {
namespace {

constexpr uint64_t kPhaseOne = 1ull << 32u;
constexpr uint32_t kMinBpmX100 = 3000u;
constexpr uint32_t kMaxBpmX100 = 30000u;
// A reset landing within this window after a pulse belongs to that pulse.
constexpr uint32_t kResetAttachUs = 5000u;
constexpr uint32_t kStopFloorUs = 250000u;

uint32_t roundedDivide(uint64_t numerator, uint64_t denominator) {
  if (denominator == 0) return 0;
  return static_cast<uint32_t>((numerator + denominator / 2u) / denominator);
}

}  // namespace

ClockSync::ClockSync(uint32_t carrier_hz) {
  setCarrierHz(carrier_hz);
  updateStopThreshold();
}

bool ClockSync::validPulsePpqn(uint8_t ppqn) {
  return ppqn == 1 || ppqn == 2 || ppqn == 4 || ppqn == 8 || ppqn == 12 ||
         ppqn == 24 || ppqn == 48;
}

void ClockSync::setCarrierHz(uint32_t carrier_hz) {
  carrier_hz_ = carrier_hz == 0 ? 1 : carrier_hz;
}

void ClockSync::setPulsePpqn(uint8_t pulse_ppqn) {
  if (!validPulsePpqn(pulse_ppqn) || pulse_ppqn == pulse_ppqn_) return;
  pulse_ppqn_ = pulse_ppqn;
  clearMeasurements();
  updateStopThreshold();
  state_ = ClockState::Stopped;
  pulse_index_ = 0;
  pending_beats_ = 0;
  pending_reset_ = false;
  intermediate_pending_ = false;
}

void ClockSync::setRestartOnStart(bool enabled) { restart_on_start_ = enabled; }

void ClockSync::clearMeasurements() {
  have_edge_ = false;
  edge_count_ = 0;
  edge_pos_ = 0;
  interval_count_ = 0;
  interval_pos_ = 0;
  quarter_count_ = 0;
  quarter_pos_ = 0;
  filtered_quarter_us_ = 0;
  jitter_us_ = 0;
}

void ClockSync::process(const ClockEvent& event) {
  switch (event.type) {
    case ClockEventType::Pulse:
      acceptPulse(event.timestamp_us);
      break;
    case ClockEventType::Reset:
      handleReset(event.timestamp_us);
      break;
  }
}

void ClockSync::acceptPulse(uint32_t timestamp_us) {
  ++accepted_events_;

  bool reset_here = pending_reset_;
  if (state_ == ClockState::Stopped) {
    if (restart_on_start_) {
      reset_here = true;
      ++restart_count_;
    }
    // A stopped clock leaves a long gap behind it; it is not a tempo interval.
    have_edge_ = false;
    edge_count_ = 0;
    edge_pos_ = 0;
    interval_count_ = 0;
    interval_pos_ = 0;
  }
  if (reset_here) {
    applyPositionReset();
    pending_reset_ = false;
  }

  if (have_edge_) {
    const uint32_t interval = timestamp_us - last_edge_us_;
    pushMedian(interval_history_, interval_count_, interval_pos_, interval);
    const uint32_t median = median3(interval_history_, interval_count_);
    const uint32_t deviation =
        interval > median ? interval - median : median - interval;
    jitter_us_ = static_cast<uint32_t>(
        static_cast<int32_t>(jitter_us_) +
        (static_cast<int32_t>(deviation) - static_cast<int32_t>(jitter_us_)) /
            4);
  }
  updateStopThreshold();
  pushEdge(timestamp_us);
  updateTempo(timestamp_us);
  last_edge_us_ = timestamp_us;
  have_edge_ = true;
  state_ = ClockState::Running;

  // Eighth-note landmark, evaluated at edge time.
  intermediate_pending_ = false;
  if (pulse_index_ % pulsesPerEighth() == 0) {
    ++pending_beats_;
    if (pulse_ppqn_ == 1) {
      // One pulse per quarter note: the odd eighth comes from phase, half an
      // estimated pulse period later. An early next pulse cancels it.
      const uint32_t half = estimatedPulseUs() / 2u;
      if (half > 0) {
        intermediate_pending_ = true;
        intermediate_due_us_ = timestamp_us + half;
      }
    }
  }
  ++pulse_index_;
}

void ClockSync::handleReset(uint32_t timestamp_us) {
  ++reset_count_;
  const bool attaches_to_last_pulse =
      state_ == ClockState::Running && have_edge_ &&
      (timestamp_us - last_edge_us_) <= kResetAttachUs;
  if (!attaches_to_last_pulse) {
    pending_reset_ = true;
    return;
  }

  // Applies to the pulse that just passed: that pulse becomes the first beat.
  applyPositionReset();
  pending_reset_ = false;
  pulse_index_ = 1u;
  ++pending_beats_;
  if (pulse_ppqn_ == 1) {
    const uint32_t half = estimatedPulseUs() / 2u;
    if (half > 0) {
      intermediate_pending_ = true;
      intermediate_due_us_ = last_edge_us_ + half;
    }
  }
}

void ClockSync::applyPositionReset() {
  pulse_index_ = 0;
  intermediate_pending_ = false;
  position_reset_pending_ = true;
}

bool ClockSync::consumePositionReset() {
  const bool reset = position_reset_pending_;
  position_reset_pending_ = false;
  return reset;
}

void ClockSync::pushEdge(uint32_t timestamp_us) {
  edge_history_[edge_pos_] = timestamp_us;
  edge_pos_ = static_cast<uint8_t>((edge_pos_ + 1u) % kEdgeHistory);
  if (edge_count_ < kEdgeHistory) ++edge_count_;
}

uint32_t ClockSync::edgeAgo(uint8_t pulses_back) const {
  const uint8_t index = static_cast<uint8_t>(
      (edge_pos_ + kEdgeHistory - 1u - pulses_back) % kEdgeHistory);
  return edge_history_[index];
}

void ClockSync::updateTempo(uint32_t timestamp_us) {
  if (edge_count_ < 2) return;
  // Measure across the last eighth note: ppqn/2 pulses, or as many as are
  // available while that window is still filling.
  uint8_t span_pulses = static_cast<uint8_t>(pulsesPerEighth());
  const uint8_t available = static_cast<uint8_t>(edge_count_ - 1u);
  if (span_pulses > available) span_pulses = available;
  if (span_pulses == 0) return;

  const uint32_t span_us = timestamp_us - edgeAgo(span_pulses);
  if (span_us == 0) return;
  // Scale the span up to one quarter note: ppqn pulses span a quarter.
  const uint32_t quarter_us = static_cast<uint32_t>(
      (static_cast<uint64_t>(span_us) * pulse_ppqn_) / span_pulses);
  if (quarter_us == 0) return;

  pushMedian(quarter_history_, quarter_count_, quarter_pos_, quarter_us);
  const uint32_t median = median3(quarter_history_, quarter_count_);
  if (filtered_quarter_us_ == 0) {
    filtered_quarter_us_ = median;
  } else {
    const int32_t delta =
        static_cast<int32_t>(median) - static_cast<int32_t>(filtered_quarter_us_);
    filtered_quarter_us_ = static_cast<uint32_t>(
        static_cast<int32_t>(filtered_quarter_us_) + delta / 2);
  }
}

void ClockSync::pushMedian(uint32_t* history, uint8_t& count, uint8_t& pos,
                           uint32_t value) {
  history[pos] = value;
  pos = static_cast<uint8_t>((pos + 1u) % kMedianHistory);
  if (count < kMedianHistory) ++count;
}

uint32_t ClockSync::median3(const uint32_t* history, uint8_t count) {
  if (count == 0) return 0;
  uint32_t values[kMedianHistory]{};
  for (uint8_t i = 0; i < count; ++i) values[i] = history[i];
  for (uint8_t i = 1; i < count; ++i) {
    const uint32_t value = values[i];
    uint8_t j = i;
    while (j > 0 && values[j - 1] > value) {
      values[j] = values[j - 1];
      --j;
    }
    values[j] = value;
  }
  return values[count / 2u];
}

uint32_t ClockSync::pulsesPerEighth() const {
  return pulse_ppqn_ >= 2u ? pulse_ppqn_ / 2u : 1u;
}

uint32_t ClockSync::estimatedPulseUs() const {
  if (filtered_quarter_us_ == 0 || pulse_ppqn_ == 0) return 0;
  return filtered_quarter_us_ / pulse_ppqn_;
}

void ClockSync::updateStopThreshold() {
  uint32_t base = median3(interval_history_, interval_count_);
  if (base == 0) {
    // No interval measured yet: allow a full pulse period at 30 BPM.
    base = 60000000u / (30u * pulse_ppqn_);
  }
  uint64_t threshold = (static_cast<uint64_t>(base) * 5u) / 2u;
  if (threshold < kStopFloorUs) threshold = kStopFloorUs;
  if (threshold > 0xfffffffful) threshold = 0xfffffffful;
  stop_threshold_us_ = static_cast<uint32_t>(threshold);
}

bool ClockSync::advanceCarrier(uint32_t now_us) {
  if (state_ == ClockState::Running && have_edge_) {
    const uint32_t elapsed = now_us - last_edge_us_;
    // A capture ISR can publish an edge between a caller's time sample and this
    // call; treat an apparent gap over half the uint32 range as a stale `now`.
    if (elapsed < 0x80000000u && elapsed > stop_threshold_us_) {
      state_ = ClockState::Stopped;
      intermediate_pending_ = false;
    }
  }

  if (pending_beats_ > 0) {
    --pending_beats_;
    return true;
  }
  if (state_ == ClockState::Running && intermediate_pending_ &&
      static_cast<int32_t>(now_us - intermediate_due_us_) >= 0) {
    intermediate_pending_ = false;
    return true;
  }
  return false;
}

uint32_t ClockSync::targetBpmX100() const {
  if (filtered_quarter_us_ == 0) return 0;
  uint32_t bpm_x100 = roundedDivide(6000000000ull, filtered_quarter_us_);
  if (bpm_x100 < kMinBpmX100) bpm_x100 = kMinBpmX100;
  if (bpm_x100 > kMaxBpmX100) bpm_x100 = kMaxBpmX100;
  return bpm_x100;
}

ClockDiagnostics ClockSync::diagnostics() const {
  return {state_,      targetBpmX100(),  jitter_us_,
          last_edge_us_, pulse_ppqn_,    restart_on_start_,
          accepted_events_, restart_count_, reset_count_};
}

uint64_t ClockSync::playbackIncrementQ32(uint32_t carrier_hz,
                                         uint32_t target_bpm_x100,
                                         uint32_t source_bpm) {
  if (carrier_hz == 0 || source_bpm == 0) return 0;
  const uint64_t numerator = 24000ull * target_bpm_x100 * kPhaseOne;
  const uint64_t denominator =
      static_cast<uint64_t>(source_bpm) * 100u * carrier_hz;
  return (numerator + denominator / 2u) / denominator;
}

const char* clockStateName(ClockState state) {
  switch (state) {
    case ClockState::Stopped:
      return "STOPPED";
    case ClockState::Running:
      return "RUNNING";
  }
  return "STOPPED";
}

}  // namespace piko
