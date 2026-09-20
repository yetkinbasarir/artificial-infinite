#pragma once

#include <stdint.h>

#include "ClockSource.h"
#include "Looper.h"
#include "MacroEngine.h"

#if PIKO_CLOCK_INTERNAL

// Master clock: a hardware alarm produces 24 PPQN ticks and sends one MIDI
// clock byte per tick. The clock starts at boot and never stops; the player
// on top of it does, and its musical position can be nudged without the clock
// moving at all. No transport messages are ever sent.

// MIDI TX sits on the pin the external build uses for clock in.
#define PIKO_MIDI_TX_PIN 22
#define PIKO_MIDI_BAUD 31250
// Start/stop button, the pin the external build uses for reset in.
#define PIKO_TRANSPORT_PIN 21
#define PIKO_TRANSPORT_DEBOUNCE_US 30000u

void piko_internal_clock_init(uint32_t tempo_x100);

// Called from the main loop: plans the next tick and draws the macro pattern
// the next variation period will use.
void piko_internal_clock_service();

// A new sample was selected; it enters on the next bar line, or at once while
// the player is stopped.
void piko_internal_clock_request_sample_tempo(uint32_t tempo_x100);

// Absolute tempo from the knob. Taking over cancels a glide.
void piko_internal_clock_set_knob_tempo(uint32_t tempo_x100);

// The bank was rewritten or erased: stop, take the tempo of the first slot
// without gliding, and clear the nudge, the macro counters and the loop. The
// clock itself keeps ticking.
void piko_internal_clock_bank_reset(uint32_t tempo_x100);

// True once, on the tick where the pending sample change takes effect.
bool piko_internal_clock_consume_sample_swap();
// True once per step the player should trigger.
bool piko_internal_clock_consume_step();
// True once after the player started, so the engine can restart its position.
bool piko_internal_clock_consume_restart();
// True once after the player stopped, so the engine can fade out.
bool piko_internal_clock_consume_stop();

bool piko_internal_clock_playing();
uint32_t piko_internal_clock_step_index();
int32_t piko_internal_clock_nudge_offset();
void piko_internal_clock_nudge(int32_t ticks);
void piko_internal_clock_clear_nudge();

// Macro knobs. Intensity is 0..65535, mode 1..5 (applied on the next bar).
void piko_internal_clock_set_macro_intensity(uint16_t intensity);
void piko_internal_clock_set_macro_mode(uint8_t mode);
uint8_t piko_internal_clock_macro_mode();
uint8_t piko_internal_clock_macro_pending_mode();
// The macro's decision for the step about to play, taken at this instant
// against the current intensity.
piko::MacroStep piko_internal_clock_macro_step();

// Looper (selector 6). Knob A holds or fades the loop, knob B sets the
// recording velocity and, at its bottom, erases.
void piko_internal_clock_looper_enable(bool on);
void piko_internal_clock_looper_set_velocity(uint8_t velocity);
void piko_internal_clock_looper_set_erasing(bool on);
void piko_internal_clock_looper_press(uint8_t button, uint8_t slice, uint32_t now_ms);
void piko_internal_clock_looper_release(uint8_t button, uint32_t now_ms);
void piko_internal_clock_looper_close(uint32_t now_ms);
bool piko_internal_clock_looper_recording();
bool piko_internal_clock_looper_defined();
// One loop event to play, taken once.
bool piko_internal_clock_consume_loop_trigger(piko::LoopTrigger* trigger);

// Tempo at the last tick, and the audio-rate interpolation between ticks.
uint32_t piko_internal_clock_tempo_x100();
uint32_t piko_internal_clock_tempo_now_x100();
bool piko_internal_clock_gliding();

// Implemented by the audio engine: called from the tick interrupt.
void piko_internal_clock_on_tick(uint32_t timestamp_us);

#endif  // PIKO_CLOCK_INTERNAL
