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
