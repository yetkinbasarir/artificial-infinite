#pragma once

#include <stdint.h>

#include "TempoEngine.h"

namespace piko {

// A live loop of button presses, held in RAM and never written to flash.
//
// Presses are stored where they fell against the player's musical position,
// at tick resolution, so a nudge carries the loop with everything else. The
// loop keeps playing whatever the selector shows; with its knob turned off it
// fades away a quarter of its velocity per pass and disappears.
static constexpr uint32_t kLoopMaxEvents = 256;
static constexpr uint32_t kLoopMaxTicks = 8u * kTicksPerBar;   // 8 bars
static constexpr uint32_t kLoopBeatTicks = kTicksPerQuarter;   // one beat
static constexpr uint8_t kLoopVelocityFloor = 16;              // fades out here
static constexpr uint32_t kLoopCloseWindowMs = 50;             // gesture window
static constexpr uint8_t kLoopButtons = 8;

struct LoopEvent {
  uint32_t press = 0;    // ticks from the loop start
  uint32_t release = 0;
  uint8_t slice = 0;
  uint8_t velocity = 0;
  uint8_t button = 0;
  bool open = false;     // the button is still down
};

struct LoopTrigger {
  bool trigger = false;
  uint8_t slice = 0;
  uint8_t velocity = 0;
  uint32_t ticks = kTicksPerStep;  // how long the event was held
};

class Looper {
 public:
  void reset();

  // Knob A: the loop records and holds while on, fades away while off.
  void setEnabled(bool on);
  bool enabled() const { return enabled_; }
  // Knob B: recording velocity, and its bottom is the erase zone.
  void setVelocity(uint8_t velocity) { velocity_ = velocity; }
  uint8_t velocity() const { return velocity_; }
  void setErasing(bool on) { erasing_ = on; }
  bool erasing() const { return erasing_; }

  bool defined() const { return length_ > 0; }
  bool recording() const { return recording_; }
  uint32_t length() const { return length_; }
  uint32_t eventCount() const { return count_; }

  // Panel events, with the player's position and a millisecond stamp.
  void pressButton(uint8_t button, uint8_t slice, uint32_t position, uint32_t now_ms);
  void releaseButton(uint8_t button, uint32_t position, uint32_t now_ms);
  // Two buttons down together on the looper's selector.
  void closeLoop(uint32_t position, uint32_t now_ms);

  // Called once per musical tick.
  LoopTrigger tick(uint32_t position);

  // The player restarted: the loop keeps its content and starts from its own
  // beginning alongside the first step.
  void onTransportStart(uint32_t position);

 private:
  void eraseButton(uint8_t button);
  void decayOnePass();
  uint32_t loopPosition(uint32_t position) const;

  LoopEvent events_[kLoopMaxEvents]{};
  uint32_t count_ = 0;

  bool enabled_ = false;
  bool erasing_ = false;
  uint8_t velocity_ = 200;

  bool recording_ = false;
  uint32_t loop_start_ = 0;
  uint32_t length_ = 0;
  uint32_t last_loop_position_ = 0;
  bool have_last_position_ = false;

  int16_t open_event_[kLoopButtons] = {-1, -1, -1, -1, -1, -1, -1, -1};
  uint32_t last_press_ms_ = 0;
  int16_t last_press_event_ = -1;
};

}  // namespace piko
