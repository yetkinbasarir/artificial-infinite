#pragma once

#include <stdint.h>

#include "TempoEngine.h"

namespace piko {

// One knob of intensity and one of mode drive every variation the player makes
// on its own. The pattern is redrawn at the start of each variation period
// from a generator seeded by the period counter and the mode, so the same
// period always sounds the same and neighbouring periods do not.
static constexpr uint32_t kMacroMaxSteps = 64;
static constexpr uint8_t kMacroModes = 5;
static constexpr uint16_t kMacroOne = 65535u;
// The first 3 % of the intensity knob is off: the macro touches nothing.
static constexpr uint16_t kMacroDeadZone = (kMacroOne * 3u) / 100u;

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

class MacroEngine {
 public:
  void reset();

  void setIntensity(uint16_t intensity);  // 0..65535
  uint16_t intensity() const { return intensity_; }
  bool active() const { return intensity_ > kMacroDeadZone; }

  // A new mode waits for the next bar line.
  void setMode(uint8_t mode);
  void applyPendingMode();
  uint8_t mode() const { return mode_; }
  uint8_t pendingMode() const { return pending_mode_; }

  // Variation period for the current mode and intensity.
  uint32_t periodTicks() const;

  // Draws the pattern for `period_index`. Call once per period.
  void beginPeriod(uint32_t period_index);
  // Draws the pattern the next period will use, so nothing is generated in
  // the tick interrupt.
  void prepareNextPeriod(uint32_t period_index);
  bool takePrepared(uint32_t period_index);

  const MacroStep& step(uint32_t step_index) const;
  uint32_t patternSteps() const { return pattern_steps_; }

 private:
  struct Pattern {
    MacroStep steps[kMacroMaxSteps];
    uint32_t length = 8;
    uint32_t period_index = 0;
    bool valid = false;
  };

  void generate(Pattern& pattern, uint32_t period_index) const;

  uint16_t intensity_ = 0;
  uint8_t mode_ = 1;
  uint8_t pending_mode_ = 1;

  Pattern pattern_{};
  Pattern prepared_{};
  uint32_t pattern_steps_ = 8;
  MacroStep neutral_{};
};

// Even spread of `pulses` over `steps`, rotated. Used for step thinning.
bool euclideanHit(uint32_t index, uint32_t pulses, uint32_t steps, uint32_t rotation);

}  // namespace piko
