#include "InternalClock.h"

#if PIKO_CLOCK_INTERNAL

#include "SpscQueue.h"
#include "TempoEngine.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "pico/stdlib.h"
#include "uart_tx.pio.h"

namespace {

constexpr uint8_t kMidiClock = 0xf8;
constexpr uint8_t kMidiStart = 0xfa;
// One tick is planned ahead; a deeper queue would only delay a sample change.
constexpr uint32_t kPlanQueueSize = 4;

PIO midi_pio = pio0;
uint midi_sm = 0;

piko::TempoEngine tempo_engine;  // main loop only
SpscQueue<piko::TickPlan, kPlanQueueSize> plan_queue;

int alarm_num = -1;
volatile bool clock_running = false;
volatile bool started_transport = false;
volatile bool sample_swap_pending = false;

// Published by the tick interrupt for audio-rate interpolation.
volatile uint32_t tick_start_us = 0;
volatile uint32_t tick_period_us = 20833;
volatile uint32_t tick_tempo_q16 = 120u * piko::kTempoQ16One;
volatile uint32_t tick_next_tempo_q16 = 120u * piko::kTempoQ16One;
volatile uint32_t last_period_us = 20833;

uint64_t next_target_us = 0;

uint32_t tempoQ16FromX100(uint32_t tempo_x100) {
  return static_cast<uint32_t>((static_cast<uint64_t>(tempo_x100) *
                                piko::kTempoQ16One) /
                               100u);
}

uint32_t tempoX100FromQ16(uint32_t tempo_q16) {
  return static_cast<uint32_t>(
      (static_cast<uint64_t>(tempo_q16) * 100u + piko::kTempoQ16One / 2u) /
      piko::kTempoQ16One);
}

void __not_in_flash_func(midi_send)(uint8_t byte) {
  // Never block the tick: a full FIFO means the port is already saturated.
  if (pio_sm_is_tx_fifo_full(midi_pio, midi_sm)) return;
  pio_sm_put(midi_pio, midi_sm, static_cast<uint32_t>(byte));
}

void __not_in_flash_func(clock_alarm_handler)(uint alarm) {
  (void)alarm;
  const uint32_t now_us = time_us_32();

  // MIDI start goes out immediately before the first clock byte.
  if (!started_transport) {
    started_transport = true;
    midi_send(kMidiStart);
  }
  midi_send(kMidiClock);

  piko::TickPlan plan{};
  uint32_t period_us = last_period_us;
  if (plan_queue.pop(plan)) {
    period_us = plan.period_us;
    last_period_us = period_us;
    tick_tempo_q16 = plan.tempo_q16;
    tick_next_tempo_q16 = plan.next_tempo_q16;
    if (plan.sample_swap) sample_swap_pending = true;
  }
  tick_start_us = now_us;
  tick_period_us = period_us;

  piko_internal_clock_on_tick(now_us);

  next_target_us += period_us;
  if (hardware_alarm_set_target(alarm_num,
                                from_us_since_boot(next_target_us))) {
    // The target had already passed (a long lockout, say). Resynchronise on
    // the current time rather than firing a burst of catch-up ticks.
    next_target_us = time_us_64() + period_us;
    hardware_alarm_set_target(alarm_num, from_us_since_boot(next_target_us));
  }
}

void midi_uart_init() {
  const uint offset = pio_add_program(midi_pio, &uart_tx_program);
  midi_sm = pio_claim_unused_sm(midi_pio, true);

  pio_sm_set_pins_with_mask(midi_pio, midi_sm, 1u << PIKO_MIDI_TX_PIN,
                            1u << PIKO_MIDI_TX_PIN);
  pio_sm_set_pindirs_with_mask(midi_pio, midi_sm, 1u << PIKO_MIDI_TX_PIN,
                               1u << PIKO_MIDI_TX_PIN);
  pio_gpio_init(midi_pio, PIKO_MIDI_TX_PIN);
  gpio_set_drive_strength(PIKO_MIDI_TX_PIN, GPIO_DRIVE_STRENGTH_12MA);

  pio_sm_config config = uart_tx_program_get_default_config(offset);
  sm_config_set_out_shift(&config, true, false, 32);  // shift right, LSB first
  sm_config_set_out_pins(&config, PIKO_MIDI_TX_PIN, 1);
  sm_config_set_sideset_pins(&config, PIKO_MIDI_TX_PIN);
  sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
  // Eight PIO cycles per bit. At 248 MHz and 31250 baud this is exactly 992.
  const float divider =
      static_cast<float>(clock_get_hz(clk_sys)) / (8.0f * PIKO_MIDI_BAUD);
  sm_config_set_clkdiv(&config, divider);
  pio_sm_init(midi_pio, midi_sm, offset, &config);
  pio_sm_set_enabled(midi_pio, midi_sm, true);
}

}  // namespace

void piko_internal_clock_init(uint32_t tempo_x100) {
  midi_uart_init();

  tempo_engine.reset(tempoQ16FromX100(tempo_x100));
  plan_queue.clear();
  const piko::TickPlan first = tempo_engine.planNextTick();
  last_period_us = first.period_us;
  tick_period_us = first.period_us;
  tick_tempo_q16 = first.tempo_q16;
  tick_next_tempo_q16 = first.next_tempo_q16;

  alarm_num = hardware_alarm_claim_unused(true);
  hardware_alarm_set_callback(alarm_num, clock_alarm_handler);
  irq_set_priority(TIMER_IRQ_0 + alarm_num, 0x00);

  clock_running = true;
  next_target_us = time_us_64() + first.period_us;
  hardware_alarm_set_target(alarm_num, from_us_since_boot(next_target_us));
}

void piko_internal_clock_service() {
  if (!clock_running) return;
  // Keep exactly one tick ready. Planning further ahead would make a sample
  // change wait past the bar line it belongs to.
  if (plan_queue.empty()) {
    plan_queue.push(tempo_engine.planNextTick());
  }
}

void piko_internal_clock_request_sample_tempo(uint32_t tempo_x100) {
  tempo_engine.requestSampleTempo(tempoQ16FromX100(tempo_x100));
}

void piko_internal_clock_set_knob_tempo(uint32_t tempo_x100) {
  tempo_engine.setKnobTempo(tempoQ16FromX100(tempo_x100));
}

bool piko_internal_clock_consume_sample_swap() {
  if (!sample_swap_pending) return false;
  sample_swap_pending = false;
  return true;
}

uint32_t piko_internal_clock_tempo_x100() {
  return tempoX100FromQ16(tick_tempo_q16);
}

// Linear interpolation between this tick's tempo and the next one, so the
// playback rate moves smoothly instead of stepping on every tick.
uint32_t __not_in_flash_func(piko_internal_clock_tempo_now_x100)() {
  const uint32_t from = tick_tempo_q16;
  const uint32_t to = tick_next_tempo_q16;
  if (from == to) return tempoX100FromQ16(from);

  const uint32_t period = tick_period_us;
  uint32_t elapsed = time_us_32() - tick_start_us;
  if (period == 0) return tempoX100FromQ16(from);
  if (elapsed > period) elapsed = period;

  const int64_t span = static_cast<int64_t>(to) - static_cast<int64_t>(from);
  const int64_t value =
      static_cast<int64_t>(from) + (span * elapsed) / static_cast<int64_t>(period);
  return tempoX100FromQ16(static_cast<uint32_t>(value));
}

bool piko_internal_clock_gliding() { return tempo_engine.gliding(); }

#endif  // PIKO_CLOCK_INTERNAL
