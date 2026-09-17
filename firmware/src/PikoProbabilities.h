#pragma once

#include <stdint.h>

// Probability settings and their slots in the saved settings page. Keeping the
// mapping in one place is what stops two probabilities from sharing a byte.
#define PIKO_SAVE_PROB_DIRECTION 8
#define PIKO_SAVE_PROB_RETRIG 9
#define PIKO_SAVE_PROB_JUMP 10
#define PIKO_SAVE_PROB_GATE 11
#define PIKO_SAVE_PROB_TUNNEL 12

struct PikoProbabilities {
  uint8_t direction;
  uint8_t jump;
  uint8_t retrig;
  uint8_t gate;
  uint8_t tunnel;
};

static inline void piko_store_probabilities(const PikoProbabilities& values,
                                            uint8_t* page) {
  page[PIKO_SAVE_PROB_DIRECTION] = values.direction;
  page[PIKO_SAVE_PROB_JUMP] = values.jump;
  page[PIKO_SAVE_PROB_RETRIG] = values.retrig;
  page[PIKO_SAVE_PROB_GATE] = values.gate;
  page[PIKO_SAVE_PROB_TUNNEL] = values.tunnel;
}

static inline PikoProbabilities piko_load_probabilities(const uint8_t* page) {
  PikoProbabilities values;
  values.direction = page[PIKO_SAVE_PROB_DIRECTION];
  values.jump = page[PIKO_SAVE_PROB_JUMP];
  values.retrig = page[PIKO_SAVE_PROB_RETRIG];
  values.gate = page[PIKO_SAVE_PROB_GATE];
  values.tunnel = page[PIKO_SAVE_PROB_TUNNEL];
  return values;
}
