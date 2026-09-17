#pragma once

#include <stdint.h>

#include "ClockSync.h"

enum class PikoRequestType : uint8_t {
  SetPulsePpqn,
  SetRestartOnStart,
  StopPlayback,
  StartPlayback,
};

struct PikoRequest {
  uint32_t id;
  PikoRequestType type;
  uint8_t value;
};

struct PikoClockSnapshot {
  piko::ClockDiagnostics clock;
  uint32_t rejected_edges;  // filtered by the capture ISR
  uint32_t clock_queue_drops;
  uint32_t midi_queue_drops;  // USB MIDI note output only
};

// Flash is a single shared device: core 0 saves settings while core 1 writes
// the sample bank, so both sides take this lock before touching it. Core 0
// additionally locks core 1 out, which keeps core 1 from reading XIP while a
// sector is erased or programmed.
void piko_flash_lock();
void piko_flash_unlock();
void piko_flash_lockout_victim_init();

// Core 1 request API. Completion is explicitly acknowledged by core 0.
bool piko_request_pulse_ppqn(uint8_t ppqn);
bool piko_request_restart_on_start(bool enabled);
bool piko_request_stop_playback();
bool piko_request_start_playback();

// Core 0 request service API.
bool piko_runtime_pop_request(PikoRequest* request);
void piko_runtime_ack_request(uint32_t id, bool ok);

// PWM/core-0 producer, TinyUSB/core-1 consumer.
void piko_usb_midi_note_on(uint8_t note, uint8_t velocity);
void piko_runtime_service_usb_midi();
uint32_t piko_usb_midi_queue_drops();

// Seqlock-protected cross-core diagnostic snapshot.
void piko_publish_clock_snapshot(const PikoClockSnapshot& snapshot);
bool piko_read_clock_snapshot(PikoClockSnapshot* snapshot);
