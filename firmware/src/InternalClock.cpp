#include "InternalClock.h"

#if PIKO_CLOCK_INTERNAL

#include "Looper.h"
#include "MacroEngine.h"
#include "SpscQueue.h"
#include "TempoEngine.h"
#include "Transport.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "pico/stdlib.h"
#include "uart_tx.pio.h"

namespace {

constexpr uint8_t kMidiClock = 0xf8;
// One tick is planned ahead; a deeper queue would only delay a bar decision.
constexpr uint32_t kPlanQueueSize = 4;

PIO midi_pio = pio0;
uint midi_sm = 0;

piko::TempoEngine tempo_engine;  // main loop only
SpscQueue<piko::TickPlan, kPlanQueueSize> plan_queue;

// Touched by the tick interrupt and read by the main loop and the audio path.
piko::Transport transport;
piko::MacroEngine macro;
piko::Looper looper;

volatile bool loop_trigger_pending = false;
piko::LoopTrigger loop_trigger{};

int alarm_num = -1;
volatile bool clock_running = false;
volatile bool sample_swap_pending = false;
volatile bool step_pending = false;
volatile bool restart_pending = false;
volatile bool stop_pending = false;
volatile bool bar_pending_glide = false;
volatile uint32_t macro_step_index = 0;

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

  // Clock out first: the player's state never delays a tick.
  midi_send(kMidiClock);

  piko::TickPlan plan{};
  uint32_t period_us = last_period_us;
  if (plan_queue.pop(plan)) {
    period_us = plan.period_us;
    last_period_us = period_us;
    tick_tempo_q16 = plan.tempo_q16;
    tick_next_tempo_q16 = plan.next_tempo_q16;
  }
  tick_start_us = now_us;
  tick_period_us = period_us;

  // Feed the shared clock path so the diagnostics keep measuring the tempo.
  piko_internal_clock_on_tick(now_us);

  const piko::TransportTick tick = transport.advanceRawTick();
  if (tick.playing) {
    if (tick.period) {
      // Drawing the dice for a period is a handful of shifts per step.
      macro.beginPeriod(tick.period_index);
      transport.setPeriodTicks(macro.periodTicks());
    }
    if (tick.bar) {
      // A new mode redraws the dice at once, and the euclidean count is
      // latched from the intensity of this bar.
      if (macro.applyPendingMode()) macro.beginPeriod(tick.period_index);
      macro.beginBar();
      bar_pending_glide = true;
    }
    if (tick.step) {
      macro_step_index = tick.step_index;
      step_pending = true;
    }
    // The loop runs at tick resolution; a nudge carries it along. Only the
    // last event of a burst is played, as with steps.
    for (uint32_t i = 0; i < tick.advanced; ++i) {
      const uint32_t at = tick.position - (tick.advanced - 1u - i);
      const piko::LoopTrigger hit = looper.tick(at);
      if (hit.trigger) {
        loop_trigger = hit;
        loop_trigger_pending = true;
      }
    }
  }

  next_target_us += period_us;
  if (hardware_alarm_set_target(alarm_num,
                                from_us_since_boot(next_target_us))) {
    // The target had already passed (a long lockout, say). Resynchronise on
    // the current time rather than firing a burst of catch-up ticks.
    next_target_us = time_us_64() + period_us;
    hardware_alarm_set_target(alarm_num, from_us_since_boot(next_target_us));
  }
}

void __not_in_flash_func(transport_button_irq)(uint gpio, uint32_t events) {
  if (gpio != PIKO_TRANSPORT_PIN || (events & GPIO_IRQ_EDGE_FALL) == 0) return;
  static uint32_t last_press_us = 0;
  static bool have_press = false;
  const uint32_t now_us = time_us_32();
  if (have_press && now_us - last_press_us < PIKO_TRANSPORT_DEBOUNCE_US) return;
  last_press_us = now_us;
  have_press = true;

  if (transport.playing()) {
    transport.requestStop();
    stop_pending = true;
  } else {
    transport.requestStart();
    restart_pending = true;
    // Starting redraws the pattern along with the counters it resets, and the
    // loop starts from its own beginning.
    macro.beginPeriod(0);
    macro.beginBar();
    looper.onTransportStart(0);
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

void transport_button_init() {
  gpio_init(PIKO_TRANSPORT_PIN);
  gpio_set_dir(PIKO_TRANSPORT_PIN, GPIO_IN);
  gpio_pull_up(PIKO_TRANSPORT_PIN);
  gpio_set_irq_enabled_with_callback(PIKO_TRANSPORT_PIN, GPIO_IRQ_EDGE_FALL,
                                     true, transport_button_irq);
  irq_set_priority(IO_IRQ_BANK0, 0x40);
  irq_set_enabled(IO_IRQ_BANK0, true);
}

}  // namespace

void piko_internal_clock_init(uint32_t tempo_x100) {
  midi_uart_init();
  transport_button_init();

  tempo_engine.reset(tempoQ16FromX100(tempo_x100));
  transport.reset();
  macro.reset();
  looper.reset();
  transport.setPeriodTicks(macro.periodTicks());

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

  if (bar_pending_glide) {
    bar_pending_glide = false;
    // The bar line has passed: start the glide and let the sample change.
    if (tempo_engine.startPendingGlide()) sample_swap_pending = true;
  }
  // Keep exactly one tick ready. Planning further ahead would make a bar
  // decision wait past the line it belongs to.
  if (plan_queue.empty()) {
    plan_queue.push(tempo_engine.planNextTick());
  }
}

void piko_internal_clock_request_sample_tempo(uint32_t tempo_x100) {
  tempo_engine.requestSampleTempo(tempoQ16FromX100(tempo_x100));
  if (!transport.playing()) {
    // Nothing is playing, so there is no bar to wait for and nothing to glide.
    if (tempo_engine.applyPendingTempoNow()) sample_swap_pending = true;
  }
}

void piko_internal_clock_set_knob_tempo(uint32_t tempo_x100) {
  tempo_engine.setKnobTempo(tempoQ16FromX100(tempo_x100));
}

void piko_internal_clock_bank_reset(uint32_t tempo_x100) {
  const uint32_t interrupts = save_and_disable_interrupts();
  transport.requestStop();
  transport.resetNudge();
  // A glide in flight is abandoned and the new tempo is taken as it is.
  tempo_engine.applyPendingTempoNow();
  tempo_engine.jumpToTempo(tempoQ16FromX100(tempo_x100));
  macro.beginPeriod(0);
  macro.beginBar();
  looper.reset();
  sample_swap_pending = false;
  step_pending = false;
  loop_trigger_pending = false;
  bar_pending_glide = false;
  restore_interrupts(interrupts);
}

bool piko_internal_clock_consume_sample_swap() {
  if (!sample_swap_pending) return false;
  sample_swap_pending = false;
  return true;
}

bool piko_internal_clock_consume_step() {
  if (!step_pending) return false;
  step_pending = false;
  return true;
}

bool piko_internal_clock_consume_restart() {
  if (!restart_pending) return false;
  restart_pending = false;
  return true;
}

bool piko_internal_clock_consume_stop() {
  if (!stop_pending) return false;
  stop_pending = false;
  return true;
}

bool piko_internal_clock_playing() { return transport.playing(); }

uint32_t piko_internal_clock_step_index() { return macro_step_index; }

int32_t piko_internal_clock_nudge_offset() { return transport.nudgeOffset(); }

void piko_internal_clock_nudge(int32_t ticks) {
  const uint32_t interrupts = save_and_disable_interrupts();
  transport.nudge(ticks);
  restore_interrupts(interrupts);
}

void piko_internal_clock_clear_nudge() {
  const uint32_t interrupts = save_and_disable_interrupts();
  transport.clearNudge();
  restore_interrupts(interrupts);
}

void piko_internal_clock_set_macro_intensity(uint16_t intensity) {
  macro.setIntensity(intensity);
}

void piko_internal_clock_set_macro_mode(uint8_t mode) { macro.setMode(mode); }

uint8_t piko_internal_clock_macro_mode() { return macro.mode(); }

uint8_t piko_internal_clock_macro_pending_mode() { return macro.pendingMode(); }

piko::MacroStep piko_internal_clock_macro_step() {
  return macro.resolveStep(macro_step_index);
}

void piko_internal_clock_looper_enable(bool on) { looper.setEnabled(on); }

void piko_internal_clock_looper_set_velocity(uint8_t velocity) {
  looper.setVelocity(velocity);
}

void piko_internal_clock_looper_set_erasing(bool on) { looper.setErasing(on); }

void piko_internal_clock_looper_press(uint8_t button, uint8_t slice,
                                      uint32_t now_ms) {
  const uint32_t interrupts = save_and_disable_interrupts();
  looper.pressButton(button, slice, transport.position(), now_ms);
  restore_interrupts(interrupts);
}

void piko_internal_clock_looper_release(uint8_t button, uint32_t now_ms) {
  const uint32_t interrupts = save_and_disable_interrupts();
  looper.releaseButton(button, transport.position(), now_ms);
  restore_interrupts(interrupts);
}

void piko_internal_clock_looper_close(uint32_t now_ms) {
  const uint32_t interrupts = save_and_disable_interrupts();
  looper.closeLoop(transport.position(), now_ms);
  restore_interrupts(interrupts);
}

bool piko_internal_clock_looper_recording() { return looper.recording(); }

bool piko_internal_clock_looper_defined() { return looper.defined(); }

bool piko_internal_clock_consume_loop_trigger(piko::LoopTrigger* trigger) {
  if (!loop_trigger_pending || trigger == nullptr) return false;
  *trigger = loop_trigger;
  loop_trigger_pending = false;
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
