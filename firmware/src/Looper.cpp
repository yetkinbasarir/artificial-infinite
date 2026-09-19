#include "Looper.h"

namespace piko {

void Looper::reset() {
  count_ = 0;
  recording_ = false;
  loop_start_ = 0;
  length_ = 0;
  last_loop_position_ = 0;
  have_last_position_ = false;
  last_press_ms_ = 0;
  last_press_event_ = -1;
  for (uint8_t i = 0; i < kLoopButtons; ++i) open_event_[i] = -1;
}

void Looper::setEnabled(bool on) { enabled_ = on; }

uint32_t Looper::loopPosition(uint32_t position) const {
  if (length_ == 0) return position - loop_start_;
  return (position - loop_start_) % length_;
}

void Looper::eraseButton(uint8_t button) {
  uint32_t kept = 0;
  for (uint32_t i = 0; i < count_; ++i) {
    if (events_[i].button == button) continue;
    events_[kept++] = events_[i];
  }
  count_ = kept;
  for (uint8_t i = 0; i < kLoopButtons; ++i) open_event_[i] = -1;
  last_press_event_ = -1;
  if (count_ == 0 && !recording_) {
    // Nothing left to play: the loop is undefined again.
    length_ = 0;
    have_last_position_ = false;
  }
}

void Looper::pressButton(uint8_t button, uint8_t slice, uint32_t position,
                         uint32_t now_ms) {
  if (button >= kLoopButtons) return;

  if (erasing_) {
    // Holding a button in the erase zone takes its events out; nothing new is
    // recorded while erasing.
    eraseButton(button);
    return;
  }
  if (!enabled_) return;

  if (!recording_ && length_ == 0) {
    // The loop starts at the top of the beat the press fell in, so the press
    // keeps its place inside that beat.
    loop_start_ = position - (position % kLoopBeatTicks);
    recording_ = true;
    have_last_position_ = false;
  }
  if (count_ >= kLoopMaxEvents) return;

  LoopEvent& event = events_[count_];
  event.press = loopPosition(position);
  event.release = event.press + kTicksPerStep;
  event.slice = slice;
  event.velocity = velocity_ == 0 ? 1 : velocity_;
  event.button = button;
  event.open = true;
  open_event_[button] = static_cast<int16_t>(count_);
  last_press_event_ = static_cast<int16_t>(count_);
  last_press_ms_ = now_ms;
  ++count_;
}

void Looper::releaseButton(uint8_t button, uint32_t position, uint32_t now_ms) {
  (void)now_ms;
  if (button >= kLoopButtons) return;
  const int16_t index = open_event_[button];
  open_event_[button] = -1;
  if (index < 0 || static_cast<uint32_t>(index) >= count_) return;
  LoopEvent& event = events_[index];
  if (!event.open) return;
  event.open = false;
  const uint32_t release = loopPosition(position);
  // A press that spans the loop point keeps at least one step of length.
  event.release = release > event.press ? release : event.press + kTicksPerStep;
}

void Looper::closeLoop(uint32_t position, uint32_t now_ms) {
  if (!recording_) return;

  if (last_press_event_ >= 0 && now_ms - last_press_ms_ <= kLoopCloseWindowMs) {
    // The two presses were one gesture: neither belongs in the loop.
    const uint32_t index = static_cast<uint32_t>(last_press_event_);
    if (index < count_) {
      const uint8_t button = events_[index].button;
      for (uint32_t i = index; i + 1 < count_; ++i) events_[i] = events_[i + 1];
      --count_;
      if (button < kLoopButtons) open_event_[button] = -1;
    }
    last_press_event_ = -1;
  }

  // Round to the nearest beat, and never shorter than one.
  const uint32_t recorded = position - loop_start_;
  uint32_t beats = (recorded + kLoopBeatTicks / 2u) / kLoopBeatTicks;
  if (beats == 0) beats = 1;
  length_ = beats * kLoopBeatTicks;
  if (length_ > kLoopMaxTicks) length_ = kLoopMaxTicks;
  recording_ = false;

  // Close any press still down, then start the loop from its beginning.
  for (uint8_t i = 0; i < kLoopButtons; ++i) {
    const int16_t index = open_event_[i];
    if (index >= 0 && static_cast<uint32_t>(index) < count_) {
      events_[index].open = false;
    }
    open_event_[i] = -1;
  }
  loop_start_ = position;
  last_loop_position_ = 0;
  have_last_position_ = false;
  if (count_ == 0) {
    length_ = 0;  // nothing was recorded after all
  }
}

void Looper::decayOnePass() {
  uint32_t kept = 0;
  for (uint32_t i = 0; i < count_; ++i) {
    const uint32_t faded = (static_cast<uint32_t>(events_[i].velocity) * 3u) / 4u;
    if (faded < kLoopVelocityFloor) continue;
    events_[i].velocity = static_cast<uint8_t>(faded);
    events_[kept++] = events_[i];
  }
  count_ = kept;
  if (count_ == 0) {
    length_ = 0;
    have_last_position_ = false;
    for (uint8_t i = 0; i < kLoopButtons; ++i) open_event_[i] = -1;
  }
}

LoopTrigger Looper::tick(uint32_t position) {
  LoopTrigger out;

  if (recording_) {
    // Eight bars without a closing gesture closes itself.
    if (position - loop_start_ >= kLoopMaxTicks) {
      length_ = kLoopMaxTicks;
      recording_ = false;
      for (uint8_t i = 0; i < kLoopButtons; ++i) {
        const int16_t index = open_event_[i];
        if (index >= 0 && static_cast<uint32_t>(index) < count_) {
          events_[index].open = false;
        }
        open_event_[i] = -1;
      }
      loop_start_ = position;
      have_last_position_ = false;
    }
    return out;
  }

  if (length_ == 0 || count_ == 0) return out;

  const uint32_t loop_position = loopPosition(position);
  if (have_last_position_ && loop_position < last_loop_position_ && !enabled_) {
    // A pass finished with the loop switched off: everything gets quieter.
    decayOnePass();
    if (length_ == 0 || count_ == 0) {
      last_loop_position_ = loop_position;
      return out;
    }
  }
  last_loop_position_ = loop_position;
  have_last_position_ = true;

  // The most recently recorded event at this tick wins.
  for (uint32_t i = count_; i > 0; --i) {
    const LoopEvent& event = events_[i - 1];
    if (event.press != loop_position) continue;
    out.trigger = true;
    out.slice = event.slice;
    out.velocity = event.velocity;
    out.ticks = event.release > event.press ? event.release - event.press
                                            : kTicksPerStep;
    break;
  }
  return out;
}

void Looper::onTransportStart(uint32_t position) {
  // The loop keeps what it holds and starts from its own beginning.
  loop_start_ = position;
  last_loop_position_ = 0;
  have_last_position_ = false;
  recording_ = false;
  for (uint8_t i = 0; i < kLoopButtons; ++i) open_event_[i] = -1;
  last_press_event_ = -1;
}

}  // namespace piko
