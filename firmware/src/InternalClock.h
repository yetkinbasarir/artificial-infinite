#pragma once

#include <stdint.h>

#include "ClockSource.h"

#if PIKO_CLOCK_INTERNAL

// Master clock: a hardware alarm produces 24 PPQN ticks, each tick sends one
// MIDI clock byte out of the hardware MIDI port and feeds the same beat path
// the external clock uses. The clock starts at boot and never stops.

// MIDI TX sits on the pin the external build uses for clock in.
#define PIKO_MIDI_TX_PIN 22
#define PIKO_MIDI_BAUD 31250

void piko_internal_clock_init(uint32_t tempo_x100);

// Called from the main loop: works out the next tick's tempo and period.
void piko_internal_clock_service();

// A new sample was selected; it enters on the next bar line.
void piko_internal_clock_request_sample_tempo(uint32_t tempo_x100);

// Absolute tempo from the knob. Taking over cancels a glide.
void piko_internal_clock_set_knob_tempo(uint32_t tempo_x100);

// True once, on the tick where the pending sample change takes effect.
bool piko_internal_clock_consume_sample_swap();

// Tempo at the last tick, and the audio-rate interpolation between ticks.
uint32_t piko_internal_clock_tempo_x100();
uint32_t piko_internal_clock_tempo_now_x100();
bool piko_internal_clock_gliding();

// Implemented by the audio engine: called from the tick interrupt.
void piko_internal_clock_on_tick(uint32_t timestamp_us);

#endif  // PIKO_CLOCK_INTERNAL
