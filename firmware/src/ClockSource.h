#pragma once

// Which clock the firmware runs from. INTERNAL makes the board the master: it
// generates its own 24 PPQN tick and sends MIDI clock out. EXTERNAL keeps the
// Phase 1 follower: clock in on GPIO 22, reset in on GPIO 21.
#define CLOCK_SOURCE_INTERNAL 0
#define CLOCK_SOURCE_EXTERNAL 1

#ifndef CLOCK_SOURCE
#define CLOCK_SOURCE CLOCK_SOURCE_INTERNAL
#endif

#if CLOCK_SOURCE == CLOCK_SOURCE_INTERNAL
#define PIKO_CLOCK_INTERNAL 1
#define PIKO_CLOCK_SOURCE_NAME "INTERNAL"
#else
#define PIKO_CLOCK_INTERNAL 0
#define PIKO_CLOCK_SOURCE_NAME "EXTERNAL"
#endif
