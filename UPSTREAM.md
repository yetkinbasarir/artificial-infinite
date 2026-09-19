# Upstream

- **Source:** https://github.com/schollz/pikocore
- **Commit:** `1af735e` (`1af735ed294ac50db24c54e7f5e26601e3dc3aa5`, "feat: improve clock sync")
- **Imported:** 2026-09-17
- **License:** MIT (upstream `LICENSE`, unchanged)

The first commit in this repository is an unmodified snapshot of upstream at
that commit. The second commit restructures it into a 2 MB-only base.

Up to and including commit `43686b4` the firmware behaved like the upstream
2 MB build (`pikocore_2mb`, `PICO_FLASH_SIZE_BYTES=2097152`), the boot2 flash
clock divider being the only exception. **Phase 1 deliberately departs from
upstream**: the clock system was rebuilt for external analog clocks (see
below). What still matches upstream is the audio engine and effects, the bank
format (v2, 12288-byte header, 128 samples, 24 kHz), the USB bank protocol,
USB descriptors, `USBD_MANUFACTURER`/`USBD_PRODUCT`, the `PIKO1 ...` INFO
header and `PIKO_FIRMWARE_RESERVE=524288`. Outside the clock rework, known
upstream bugs were intentionally not fixed.

### Exception: boot2 flash SPI clock divider 2 → 4 (commit `43686b4`)

The firmware overclocks the RP2040 to 248 MHz. With the default boot2 divider
of 2, flash XIP reads run at ~124 MHz. On some 2 MB Pico boards this is too
fast for the flash chip: the bank header was read back corrupted and the board
reported its samples as empty. `firmware/CMakeLists.txt` now builds a
`slower_boot2` stage with `PICO_FLASH_SPI_CLKDIV=4`, which brings the flash
clock down to ~62 MHz. This has no effect on audio performance.

Because the firmware now uses its own boot2 target, which is built from the SDK's
`compile_time_choice.S`, `picotool info` reports `boot2_name: compile_time_choice`
instead of `boot2_w25q080`. The longer name also shifts a few addresses in the
binary; the code is otherwise unchanged.

## Removed

- `audio2h/`, `go.mod`, `go.sum` (Go audio converter)
- `build/` (including the committed `pikocore.uf2`)
- `main.cpp` at the root (one-line include of `src/main.cpp`)
- `target_compile_definitions.cmake.default`, plus the fallback and "stale" check in CMake
- `web/public/*.uf2` (UF2 files are now build outputs)
- `.github/workflows/build.yml` (replaced by `firmware.yml` and `pages.yml`)
- 4 MB and 16 MB firmware targets and the 16 MB `copy_if_different` post-build step

## Moved

| Upstream                           | Here                                        |
| ---------------------------------- | ------------------------------------------- |
| `src/`                             | `firmware/src/`                             |
| `doth/`                            | `firmware/doth/`                            |
| `cmake/`                           | `firmware/cmake/`                           |
| `tests/`                           | `firmware/tests/`                           |
| `CMakeLists.txt`                   | `firmware/CMakeLists.txt`                   |
| `Makefile`                         | `firmware/Makefile`                         |
| `pico_sdk_import.cmake`            | `firmware/pico_sdk_import.cmake`            |
| `tusb_config.h`                    | `firmware/tusb_config.h`                    |
| `target_compile_definitions.cmake` | `firmware/target_compile_definitions.cmake` |
| `requirements.txt`                 | `firmware/requirements.txt`                 |
| `web/`                             | `web/` (unchanged location)                 |

## Changed

- CMake: `project(artificial_infinite)`, a single target
  `add_pikocore_firmware(artificial_infinite "artificial-infinite-2mb" 2097152)`.
  `PICO_SDK_PATH` is looked up in this order: env, `../pico-sdk` (next to the
  repo), `firmware/pico-sdk`, `<repo root>/pico-sdk`.
- Makefile: builds `firmware/build/artificial-infinite-2mb.uf2` and copies it
  to `web/public/`. Go was removed from `prereqs`, and web targets use `../web`.
- Web: a single firmware option (`artificial-infinite-2mb.uf2`) with the
  selector removed. Also changed: package name `artificial-infinite-loader`,
  page title "Artificial Infinite", and Vite `base` read from `VITE_BASE`.
  Serial, bank and protocol code is unchanged.

## Phase 1: conventional external clock

The clock system was rebuilt around a conventional analog pulse clock so that
several boards can be run in sync. Audio engine, effects, bank format and the
USB bank protocol (R/W/E/I/S/B/U/X/D) are unchanged.

Removed:

- Internal tempo: `param_set_bpm`, the knob B tempo control (selector 8 is now
  an empty slot), the `SAVE_BPM` record and the LED tempo readout.
- MIDI clock and one-wire MIDI input: `doth/onewiremidi.{h,pio}`, the PIO1 IRQ
  and byte queue, MIDI note-in, the `MIDI_*` build options, the `C` serial
  command and the stored clock-input mode. USB MIDI note output stays.
- Trigger output: `doth/trigger_out.h` and the `TRIGO_PIN` output.
- The lock-clock button combination (1+2+5+6); its behaviour is now the only
  mode, so the position always comes from the global beat counter.
- The old PLL/slew/holdover clock recovery, its missed-pulse estimation and
  tempo-candidate logic.

Added or changed:

- Clock input on GPIO 22 and a new reset input on GPIO 21, both pulled up and
  captured on the falling edge, with glitch filtering in the ISR (300 µs for
  clock, 50 ms for reset). GPIO 23 is driven high, so `WS2812_ENABLED=0`.
- Beats are eighth notes emitted at edge time. 1, 2, 4, 8, 12, 24 and 48 PPQN
  are supported (default 24); 1 PPQN interpolates the off-beat eighth from the
  tempo estimate.
- Stop detection from an adaptive threshold (2.5 times the median of the last
  three intervals, at least 250 ms), plus restart-on-start and reset-input
  alignment. `SAVE_RESTART_ON_START` stores the setting; the new `T` command
  sets it.
- `INFO` drops `CLOCK_INPUT`, reports `CLOCK_SYNC_VERSION 2` and adds
  `RESTART_ON_START`. The `D` diagnostics line reports the new state machine.
- `handle_write` answers `ERR` when the written bank does not read back as
  valid.
- Web loader: pulse-division selector with the seven divisions, a
  restart-on-start checkbox, updated diagnostics, and the ittybittymidi mode
  control removed.

## Package A: DJ filter, volume and fixes

Audio path:

- The digital low-pass (`doth/biquad.py`, `doth/filter.h`, `LPF_MAX`) is
  replaced by `doth/djfilter.h`: a single-pot 4th-order Linkwitz-Riley
  isolator (two Butterworth sections, Q = 1/sqrt(2), no resonance) in a TPT
  state-variable form with Q16 state and Q24 coefficients. Coefficients come
  from 64-point tables per half, generated by `doth/generate_djfilter.py` and
  linearly interpolated, so there is no trigonometry at run time. The middle
  ±5 % of the pot is a bit-exact bypass, and the cutoff is smoothed by 1/1024
  of the remaining distance per audio sample. The retrigger sweep now pushes
  the same low-pass arm.
- Selector 8 / knob A is a linear output gain (0..256) instead of the old
  three-zone control. The wavefold distortion stage, `DISTORTION_MAX`,
  `volume_reduce`, `volume_mod` and `easings/distortion.txt` are gone; the
  noise gate and the retrigger volume shape are unchanged and still run before
  the gain. `SAVE_VOLUME` now stores the gain.
- Removed along with them: `filter_q`, `hpf_fc`, `button_filter`,
  `button_filter_on` and `retrig_filter_change`, none of which had an effect.

Fixes:

- `Sequencer::Record` stops at 128 steps instead of writing past `mem`.
- Probability slots: `param_set_break` wrote `probability_jump_` into the
  retrigger slot and `probability_direction_` into the gate slot, and two knob
  handlers saved the wrong variable. Storage now goes through
  `src/PikoProbabilities.h`, so each probability owns its byte.
- `param_set_volume` assigned to a global instead of its own reference
  parameter; the new gain function takes the value it writes.
- Flash is shared by both cores: core 0's settings save and core 1's bank
  write/erase now take one mutex, and core 0 additionally locks core 1 out
  (`multicore_lockout`, victim initialised on core 1) so nothing reads XIP
  while a sector is erased or programmed. Settings loading takes the same lock.

Tests: `firmware/tests/dsp_test.cpp` covers the filter (bit-exact bypass,
low-pass and high-pass extremes, a full pot sweep without output steps, the
retrigger sweep), the sequencer cap and the probability round trip.

## Master clock build

The board is now a master by default: `src/ClockSource.h` selects
`CLOCK_SOURCE_INTERNAL` (default) or `CLOCK_SOURCE_EXTERNAL`, and CMake builds
both UF2s. None of the Phase 1 follower code was deleted; it lives behind the
external flag.

- `src/TempoEngine.{h,cpp}`: tempo as Q16 BPM, the smootherstep glide, the
  2-bar/4-bar length rule, and per-tick periods with the sub-microsecond
  remainder carried into the next tick.
- `src/InternalClock.{h,cpp}`: a hardware alarm produces 24 PPQN ticks that
  feed the same beat path a captured clock edge does. The tick interrupt only
  reads a prepared period, re-arms the alarm and pushes a MIDI byte; the main
  loop does the tempo and period arithmetic. Both the interrupt and the sender
  are `__not_in_flash_func`.
- `doth/uart_tx.pio`: MIDI out at 31 250 baud on GPIO 22 (every hardware UART
  TX pin is taken by LEDs or knobs), 12 mA drive. One `0xFA` before the first
  `0xF8`, then one `0xF8` per tick, written without blocking.
- Tempo follows the selected sample; tunnel jumps varispeed to the running
  tempo instead of changing it. The tempo knob returns on selector 7 / knob B
  with pickup, and takes over from a glide.
- The master build performs no flash writes while playing: the knob save,
  the auto-save timer and the `P`/`T` commands are disabled there, so the
  clock is never stopped by a flash erase.
- `INFO` gains `CLOCK_SOURCE`; the web loader hides the pulse-division and
  restart settings when it reads `INTERNAL`.
- Web loader: BPM is read from the file name or estimated from the length
  (4/8/16/32 beats, 80–180 BPM), shown per row as an editable field, and a
  bank with a missing BPM cannot be uploaded.
- Master build additions: `src/Transport.{h,cpp}` keeps the player's musical
  position (clock ticks plus a nudge offset of at most ±96 ticks) and its
  start/stop state, while `src/MacroEngine.{h,cpp}` draws a variation pattern
  per period from a seeded generator (five modes, one intensity knob). The
  tempo knob moved to selector 1 / knob B, selector 7 became macro intensity
  and mode, and on selector 1 the buttons nudge instead of firing slices. The
  board boots stopped and silent on slot 1, GPIO 21 is a start/stop button on
  that build, stopping fades out over 5 ms, and no MIDI transport message is
  sent at all — the port carries `0xF8` and nothing else.
- Master refinements: the slice index follows the player's musical position
  rather than a trigger count; a sample change keeps the new attack and fades
  the old read head over 8 ms instead of crossfading heads; every mute and
  unmute (transport, serial `S`/`B`, bank writes) shares one 5 ms ramp; the
  macro decides each step against the live intensity, latching the euclidean
  count per bar and redrawing on a mode change or a start; `save_settings()`
  and the code that only serves it are compiled out of the master build, so
  the only runtime flash writes left are the loader's bank commands; the macro
  LED is timed from `time_us_64`; and the loader folds a BPM outside 40–300
  into range, marking the row.
- Loader tempo analysis: `web/src/bpm.ts` analyses a dropped batch together
  (spectral flux onset envelope over a hand-written radix-2 FFT, autocorrelation
  with a log-normal tempo prior, octave settled by length matches within the
  batch — which pick the sample's own candidate rather than copying the
  sibling's tempo — then onset density, then the 80–180 range) and records
  where each BPM came from. Agreement with the estimate allows the half, double
  and three-against-two readings, and only the weaker rules or a disagreement
  raise a flag. Leading numbers in file names (60–250) count as a stated BPM. Rows preview with a click at
  their own BPM, slots are ordered by centi-BPM then name, and
  `web/src/zip.ts` is a store-only ZIP writer for the named-copies download.
  No new dependencies.
- **Bank format v3**: `source_bpm` is centi-BPM in the same 16-bit field, so
  fractional sample tempos reach the tempo engine intact. The loader writes v3
  and still reads v2 (scaling whole BPM up); firmware rejects a v2 bank found
  in flash, so banks written before this change must be uploaded again.

## Pulling changes from upstream

Paths differ from upstream (most files live under `firmware/`), so
cherry-picks may need `-X subtree=firmware` or manual path fixes.

```sh
git remote add upstream https://github.com/schollz/pikocore   # once
git fetch upstream
git log --oneline 1af735e..upstream/main                        # review new commits
git cherry-pick <sha>                                           # or: git cherry-pick -X subtree=firmware <sha>
```

After pulling changes in, update the commit reference above.
