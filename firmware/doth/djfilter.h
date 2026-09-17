#pragma once

#include <stdint.h>

#include "doth/djfilter_tables.h"

// Single-pot DJ isolator: low-pass on the left half of the pot, a bypass
// window in the middle, high-pass on the right half. 4th-order
// Linkwitz-Riley (two identical Butterworth sections, Q = 1/sqrt(2)), no
// resonance.
//
// Audio arrives as 8-bit unsigned. It is centred to a signed Q16 sample before
// filtering and rounded back to 0..255 afterwards. Coefficients come from
// compile-time tables with linear interpolation between points, so there is no
// trigonometry at run time.

#define PIKO_DJ_KNOB_MAX 4095
// The middle +-5 % of the pot is a hard bypass.
#define PIKO_DJ_BYPASS_LO 1843
#define PIKO_DJ_BYPASS_HI 2252
// Position is signed Q16: -65536 = fully closed low-pass, 0 = bypass,
// +65536 = fully open high-pass.
#define PIKO_DJ_POSITION_ONE 65536
// Each audio sample moves the smoothed position 1/1024 of the way to target.
#define PIKO_DJ_SMOOTH_SHIFT 10

struct DjFilterStage {
  int32_t ic1eq;
  int32_t ic2eq;
};

struct DjFilter {
  // Smoothed pot position, signed Q16; target is where the pot points now.
  int32_t position;
  int32_t target;
  DjFilterStage stage[2];
};

static inline void dj_filter_reset(DjFilter* f) {
  for (uint8_t i = 0; i < 2; i++) {
    f->stage[i].ic1eq = 0;
    f->stage[i].ic2eq = 0;
  }
}

static inline void dj_filter_init(DjFilter* f) {
  f->position = 0;
  f->target = 0;
  dj_filter_reset(f);
}

// Maps a raw pot reading to a signed Q16 position.
static inline int32_t dj_filter_position_from_knob(uint16_t knob) {
  if (knob > PIKO_DJ_KNOB_MAX) knob = PIKO_DJ_KNOB_MAX;
  if (knob < PIKO_DJ_BYPASS_LO) {
    const int32_t span = PIKO_DJ_BYPASS_LO;
    return -(int32_t)(((int64_t)(span - knob) * PIKO_DJ_POSITION_ONE) / span);
  }
  if (knob > PIKO_DJ_BYPASS_HI) {
    const int32_t span = PIKO_DJ_KNOB_MAX - PIKO_DJ_BYPASS_HI;
    return (
        int32_t)(((int64_t)(knob - PIKO_DJ_BYPASS_HI) * PIKO_DJ_POSITION_ONE) /
                 span);
  }
  return 0;
}

static inline void dj_filter_set_knob(DjFilter* f, uint16_t knob) {
  f->target = dj_filter_position_from_knob(knob);
}

// One smoothing step; call once per audio sample before processing.
static inline void dj_filter_advance(DjFilter* f) {
  const int32_t delta = f->target - f->position;
  if (delta == 0) return;
  int32_t step = delta >> PIKO_DJ_SMOOTH_SHIFT;
  if (step == 0) step = delta > 0 ? 1 : -1;
  f->position += step;
}

// Interpolates one coefficient set out of a 64-point table. `amount` is the
// Q16 distance from the bypass edge towards the extreme of that half.
static inline void dj_filter_coefficients(const int32_t table[][3],
                                          int32_t amount, int32_t* out) {
  if (amount < 0) amount = 0;
  if (amount > PIKO_DJ_POSITION_ONE) amount = PIKO_DJ_POSITION_ONE;
  const int32_t scaled =
      (int32_t)(((int64_t)amount * (PIKO_DJ_FILTER_POINTS - 1)) >> 16);
  int32_t index = scaled;
  if (index >= PIKO_DJ_FILTER_POINTS - 1) {
    out[0] = table[PIKO_DJ_FILTER_POINTS - 1][0];
    out[1] = table[PIKO_DJ_FILTER_POINTS - 1][1];
    out[2] = table[PIKO_DJ_FILTER_POINTS - 1][2];
    return;
  }
  // Fractional part between index and index + 1, in Q16.
  const int32_t step =
      (int32_t)(((int64_t)PIKO_DJ_POSITION_ONE) / (PIKO_DJ_FILTER_POINTS - 1));
  const int32_t frac = amount - index * step;
  for (uint8_t i = 0; i < 3; i++) {
    const int32_t a = table[index][i];
    const int32_t b = table[index + 1][i];
    out[i] = a + (int32_t)(((int64_t)(b - a) * frac) / step);
  }
}

// One TPT state-variable section. Returns the low-pass output and, through
// `hp_out`, the matching high-pass output.
static inline int32_t dj_filter_stage(DjFilterStage* s, int32_t in_q16,
                                      const int32_t* c, int32_t* hp_out) {
  const int32_t v3 = in_q16 - s->ic2eq;
  const int32_t v1 =
      (int32_t)(((int64_t)c[0] * s->ic1eq + (int64_t)c[1] * v3) >>
                PIKO_DJ_FILTER_COEFF_SHIFT);
  const int32_t v2 =
      s->ic2eq + (int32_t)(((int64_t)c[1] * s->ic1eq + (int64_t)c[2] * v3) >>
                           PIKO_DJ_FILTER_COEFF_SHIFT);
  s->ic1eq = 2 * v1 - s->ic1eq;
  s->ic2eq = 2 * v2 - s->ic2eq;
  if (hp_out != 0) {
    // k = 1/Q = sqrt(2) in Q24.
    const int32_t kq24 = 23726566;
    *hp_out = in_q16 - (int32_t)(((int64_t)kq24 * v1) >> 24) - v2;
  }
  return v2;
}

// Filters one 8-bit unsigned sample. `position_q16` is the smoothed pot
// position, optionally pushed further into the low-pass half by an effect.
static inline uint8_t dj_filter_process(DjFilter* f, uint8_t sample,
                                        int32_t position_q16) {
  if (position_q16 == 0) {
    // Hard bypass: the sample passes through bit for bit.
    dj_filter_reset(f);
    return sample;
  }

  int32_t coeffs[3];
  const bool highpass = position_q16 > 0;
  dj_filter_coefficients(highpass ? kDjFilterHighpass : kDjFilterLowpass,
                         highpass ? position_q16 : -position_q16, coeffs);

  int32_t value = ((int32_t)sample - 128) << 16;
  for (uint8_t i = 0; i < 2; i++) {
    int32_t hp = 0;
    const int32_t lp = dj_filter_stage(&f->stage[i], value, coeffs, &hp);
    value = highpass ? hp : lp;
  }

  // Round to 8 bits and clamp.
  const int32_t rounded = (value + (1 << 15)) >> 16;
  const int32_t out = rounded + 128;
  if (out < 0) return 0;
  if (out > 255) return 255;
  return (uint8_t)out;
}
