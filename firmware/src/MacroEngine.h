#pragma once

#include <stdint.h>

#include "TempoEngine.h"

namespace piko {

// One knob of intensity and one of mode drive every variation the player makes
// on its own.
//
// A period draws the randomness, not the decisions: each step gets its dice
// rolls and a jump target up front, seeded by the period counter and the mode,
// so a period always sounds the same. Whether a roll turns into a jump, a
// reversal or a dropped step is decided at the step itself against the
// intensity of that moment, so turning the knob is heard on the next step.
static constexpr uint32_t kMacroMaxSteps = 64;
static constexpr uint8_t kMacroModes = 5;
static constexpr uint16_t kMacroOne = 65535u;
// The first 3 % of the intensity knob is off: the macro touches nothing.
static constexpr uint16_t kMacroDeadZone = (kMacroOne * 3u) / 100u;
static constexpr uint32_t kMacroStepsPerBar = kTicksPerBar / kTicksPerStep;

enum class MacroMode : uint8_t {
  Thin = 1,     // shorter gates, fewer steps
  Shuffle = 2,  // steps jump to other slices
  Roll = 3,     // retrigger rolls at the end of bars
  Reverse = 4,  // steps play backwards, then jump as well
  Abstract = 5,  // all of it, chained along one axis
};

struct MacroStep {
  bool play = true;
  uint8_t gate_percent = 100;  // of the slice length
  bool jump = false;
  uint8_t jump_slice = 0;
  bool reverse = false;
  bool roll = false;
  uint8_t roll_division = 16;  // 16th or 32nd note retriggers
};

// The dice for one step, drawn once per period.
struct MacroSlot {
  uint16_t first = 0;
  uint16_t second = 0;
  uint8_t jump_slice = 0;
};

class MacroEngine {
 public:
  void reset();

  void setIntensity(uint16_t intensity);  // 0..65535
  uint16_t intensity() const { return intensity_; }
  bool active() const { return intensity_ > kMacroDeadZone; }

  // A new mode waits for the next bar line, and redraws the pattern there.
  void setMode(uint8_t mode);
  bool applyPendingMode();  // true when the mode actually changed
  uint8_t mode() const { return mode_; }
  uint8_t pendingMode() const { return pending_mode_; }

  // Variation period for the current mode and intensity.
  uint32_t periodTicks() const;

  // Draws the dice for a period. Cheap enough for the tick interrupt.
  void beginPeriod(uint32_t period_index);
  // Latches the euclidean step count from the intensity of this bar.
  void beginBar();

  // What the macro does to the step at this musical position.
  MacroStep resolveStep(uint32_t step_index) const;
  uint32_t patternSteps() const { return length_; }
  uint32_t barSteps() const { return bar_keep_; }

 private:
  uint16_t intensity_ = 0;
  uint8_t mode_ = 1;
  uint8_t pending_mode_ = 1;

  MacroSlot slots_[kMacroMaxSteps]{};
  uint32_t length_ = kMacroStepsPerBar;
  uint32_t rotation_ = 0;
  uint32_t period_index_ = 0;
  uint32_t bar_keep_ = kMacroStepsPerBar;
};

// Even spread of `pulses` over `steps`, rotated. Used for step thinning.
bool euclideanHit(uint32_t index, uint32_t pulses, uint32_t steps, uint32_t rotation);

}  // namespace piko
