#pragma once

#include <stddef.h>
#include <stdint.h>

namespace piko {

// Captured input events. Both arrive from GPIO capture; glitch filtering
// happens in the capture ISR, so every event handed here is accepted.
enum class ClockEventType : uint8_t {
  Pulse = 0,
  Reset = 1,
};

enum class ClockState : uint8_t {
  Stopped = 0,
  Running = 1,
};

struct ClockEvent {
  ClockEventType type;
  uint32_t timestamp_us;
};

struct ClockDiagnostics {
  ClockState state;
  uint32_t bpm_x100;  // playback tempo estimate, clamped; 0 until measured
  uint32_t jitter_us;
  uint32_t last_edge_us;
  uint8_t pulse_ppqn;
  bool restart_on_start;
  uint32_t accepted_events;
  uint32_t restart_count;
  uint32_t reset_count;
};

// Conventional external analog clock follower.
//
// Beats are eighth notes and are emitted at edge time: the pulse that lands on
// an eighth-note landmark raises a beat on the first carrier tick that sees it.
// Nothing is predicted, phase-shifted or snapped to a grid, so several boards
// fed from the same clock stay sample-aligned.
class ClockSync {
 public:
  explicit ClockSync(uint32_t carrier_hz = 1);

  void setCarrierHz(uint32_t carrier_hz);
  // Changing the division drops the tempo estimate and stops the transport;
  // the next pulse starts a fresh alignment.
  void setPulsePpqn(uint8_t pulse_ppqn);
  void setRestartOnStart(bool enabled);

  void process(const ClockEvent& event);

  // Called once per PWM carrier IRQ. Returns true for one eighth-note beat.
  bool advanceCarrier(uint32_t now_us);

  // True once after the position must be reset to the first beat.
  bool consumePositionReset();

  ClockState state() const { return state_; }
  uint8_t pulsePpqn() const { return pulse_ppqn_; }
  bool restartOnStart() const { return restart_on_start_; }
  uint32_t carrierHz() const { return carrier_hz_; }
  // Tempo used for playback rate, clamped to 30..300 BPM. 0 until measured.
  uint32_t targetBpmX100() const;
  ClockDiagnostics diagnostics() const;

  static bool validPulsePpqn(uint8_t ppqn);
  static uint64_t playbackIncrementQ32(uint32_t carrier_hz,
                                       uint32_t target_bpm_x100,
                                       uint32_t source_bpm);

 private:
  static constexpr uint8_t kEdgeHistory = 25u;  // 48 PPQN needs 24 intervals
  static constexpr uint8_t kMedianHistory = 3u;

  void acceptPulse(uint32_t timestamp_us);
  void handleReset(uint32_t timestamp_us);
  void applyPositionReset();
  void clearMeasurements();
  void pushEdge(uint32_t timestamp_us);
  uint32_t edgeAgo(uint8_t pulses_back) const;
  void updateTempo(uint32_t timestamp_us);
  void pushMedian(uint32_t* history, uint8_t& count, uint8_t& pos,
                  uint32_t value);
  static uint32_t median3(const uint32_t* history, uint8_t count);
  uint32_t pulsesPerEighth() const;
  uint32_t estimatedPulseUs() const;
  void updateStopThreshold();

  uint32_t carrier_hz_ = 1;
  uint8_t pulse_ppqn_ = 24;
  bool restart_on_start_ = true;
  ClockState state_ = ClockState::Stopped;

  // Index of the next pulse to arrive; a pulse whose index is a multiple of
  // pulsesPerEighth() is an eighth-note landmark.
  uint32_t pulse_index_ = 0;
  bool have_edge_ = false;
  uint32_t last_edge_us_ = 0;

  uint32_t edge_history_[kEdgeHistory]{};
  uint8_t edge_count_ = 0;
  uint8_t edge_pos_ = 0;

  uint32_t interval_history_[kMedianHistory]{};  // raw pulse intervals
  uint8_t interval_count_ = 0;
  uint8_t interval_pos_ = 0;

  uint32_t quarter_history_[kMedianHistory]{};  // per-pulse tempo measurements
  uint8_t quarter_count_ = 0;
  uint8_t quarter_pos_ = 0;
  uint32_t filtered_quarter_us_ = 0;

  // Recomputed per pulse; advanceCarrier runs in the audio ISR.
  uint32_t stop_threshold_us_ = 0;
  uint32_t jitter_us_ = 0;
  uint32_t pending_beats_ = 0;
  bool pending_reset_ = false;         // waiting for the next pulse
  bool position_reset_pending_ = false;  // handed to the audio engine
  bool intermediate_pending_ = false;  // 1 PPQN off-beat eighth
  uint32_t intermediate_due_us_ = 0;

  uint32_t accepted_events_ = 0;
  uint32_t restart_count_ = 0;
  uint32_t reset_count_ = 0;
};

const char* clockStateName(ClockState state);

}  // namespace piko
