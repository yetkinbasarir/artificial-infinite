# Artificial Infinite

[![firmware](https://github.com/yetkinbasarir/artificial-infinite/actions/workflows/firmware.yml/badge.svg)](https://github.com/yetkinbasarir/artificial-infinite/actions/workflows/firmware.yml)

Artificial Infinite is a lo-fi sample mangler firmware based on
[schollz/pikocore](https://github.com/schollz/pikocore), trimmed down to target
**only the 2 MB Raspberry Pi Pico**. Firmware behaviour is identical to the
upstream 2 MB build except for the boot2 flash SPI clock divider and the
rebuilt external clock system; see [UPSTREAM.md](UPSTREAM.md) for details.

## Flash layout (2 MB)

| Offset      | Size      | Contents                                                        |
| ----------- | --------- | --------------------------------------------------------------- |
| `0x000000`  | 512 KB    | Firmware reserve (`PIKO_FIRMWARE_RESERVE`)                      |
| `0x07F000`  | 4 KB      | Settings (last sector of the firmware reserve)                  |
| `0x080000`  | 12 KB     | Sample bank header (v3, up to 128 samples)                      |
| `0x083000`  | ~1.56 MB  | Audio: 1,560,576 bytes ≈ 65 s of 8-bit audio at 24 kHz          |

## Clock

Two builds come out of one source tree, chosen with the `CLOCK_SOURCE` flag:

| Build | UF2 | Clock |
| ----- | --- | ----- |
| `CLOCK_SOURCE_INTERNAL` (default) | `artificial-infinite-2mb.uf2` | Master: the board makes its own 24 PPQN clock and sends MIDI clock out |
| `CLOCK_SOURCE_EXTERNAL` | `artificial-infinite-2mb-external.uf2` | Follower: analog clock in on GPIO 22, reset in on GPIO 21 |

The web loader and the published UF2 use the master build.

### Master (default)

- The clock starts at boot and never stops, and **MIDI clock is the only thing
  the port ever sends**: no start, continue or stop messages.
- 24 PPQN, 4/4, so a bar is 96 ticks. One beat is an eighth note.
- The board wakes up **stopped and silent**, on the sample in slot 1 whatever
  was saved last, at that sample's tempo.
- **Start/stop: the button on GPIO 21** (pull-up, falling edge, 30 ms
  debounce). Starting plays from the first step on the next tick, redraws the
  macro pattern and clears its counters and the nudge. Every way of going
  quiet — the transport, the serial stop command, a bank write — rides the
  same 5 ms fade, and so does coming back. The clock and its MIDI keep running
  either way. There is no mute button combination on this build.
- A new sample enters at full level on the bar line with its attack intact;
  the sample it replaced keeps sounding for 8 ms and fades out.
- The slice number comes from the player's musical position, so a nudge
  carries the sequence along with it. Jumps, tunnel and the macro are applied
  on top of that.
- The tempo comes from the selected sample's BPM. Selecting another sample
  glides to its tempo while playing, and simply takes it while stopped.
- Tempo can be taken over by the tempo knob (selector 1, knob B): absolute
  40–300 BPM. The knob does nothing until its position matches the running
  tempo, so a sample change never snaps the tempo to wherever the pot sits.
  Catching the tempo during a glide cancels that glide.

#### Macro (selector 7)

One knob of intensity (knob A) and one of mode (knob B) drive every variation
the board makes on its own. The first 3 % of the intensity knob is off. Both
knobs pick up rather than jump, and the macro keeps its setting when the
selector moves away. A new mode waits for the next bar line and redraws the
pattern there; while the mode knob moves, the LED of the mode number lights
for a second.

Each variation period draws the dice, not the decisions: every step gets its
random values up front, seeded by the period counter and the mode, so a period
always sounds the same and the next one does not. Whether a roll becomes a
jump, a reversal or a dropped step is decided at the step itself against the
intensity of that moment, so **turning the knob is heard on the next step**.
The euclidean step count is taken from the intensity at each bar line, and the
period length from the intensity when the period starts. Periods are 8, 4, 2 or 1 bars, or 2 beats or
1 beat; more intensity means a shorter period.

| Mode | What it does | Period |
| ---- | ------------ | ------ |
| 1 Thin | Gates shorten from 100 % to 25 % over the first half of the knob; past 0.3 the bar loses steps, spread evenly, never below two | 8 → 2 bars |
| 2 Shuffle | Up to three quarters of the steps jump to another slice of the same sample; nothing is dropped | 4 bars → 1 beat |
| 3 Roll | Retrigger rolls at the end of bars: every fourth bar, half a beat, sixteenths at the bottom of the knob; every bar, two beats, thirty-seconds at the top | 4 → 1 bar |
| 4 Reverse | Up to 60 % of the steps play backwards; past half the knob some of them jump as well | 2 bars → 1 beat |
| 5 Abstract | One chain: gates and thinning, then jumps, then a few shortest-gate rolls | 8 bars → 1 beat |

#### Looper (selector 6)

The looper replaces the sequencer on the master build; nothing it records ever
reaches flash.

- **Knob A** holds the loop on its right half and lets it fade on the left.
  **Knob B** sets the recording velocity, and its bottom tenth is the erase
  zone. Both knobs pick up rather than jump.
- With no loop yet and knob A on, the first button press starts recording. The
  loop begins at the top of the beat that press fell in, so the press keeps its
  place inside the beat. Presses are stored at tick resolution against the
  player's position, up to 256 of them, so a nudge carries the loop too.
- Pressing a second button while one is down closes the loop, rounded to the
  nearest beat and never shorter than one. The loop returns to its start and
  plays at once. If the second press came within 50 ms of the first, that first
  press is taken back out — the gesture is not recorded. Left open, the loop
  closes itself at eight bars.
- Once closed, further presses overdub. Where two events land on the same tick,
  the one pressed last is heard. Each event plays its slice for as long as the
  button was held, at the velocity it was recorded with.
- Holding a button while knob B sits in the erase zone takes that button's
  events out of the loop, and records nothing meanwhile.
- With knob A off the loop keeps playing but loses a quarter of its velocity
  every pass; events that fall below the floor go, and once none are left the
  loop is free for a new recording.
- The loop plays on whatever the selector shows. Two buttons together only
  close the loop on selector 6; elsewhere they still start a retrigger.
- Stopping the transport keeps the loop and silences it; starting plays it from
  its own beginning alongside the first step.

#### Freeze (selector 8, knob A)

The right half of the knob holds the slice that was playing and retriggers it
at every step boundary, from the next boundary on. While it is held, nothing
else picks the slice: no jumps, tunnel, reverse, gate, retrigger chance, macro,
loop event or live button. The musical position, the macro counters and the
loop keep running underneath, so letting go returns the player to where it
would have been. Knob B on this selector does nothing, and its buttons are
ordinary slice buttons.

#### Nudge (selector 1 buttons)

On selector 1 the eight buttons do not fire slices: they shift the player
against the clock, in ticks. Buttons 1–4 are +1, +6, +12 and +24; buttons 5–8
are −24, −12, −6 and −1. A forward nudge plays the skipped ticks at once and
triggers only the last step boundary it crosses; a backward nudge swallows the
ticks to come. Buttons 1 and 8 repeat after 400 ms every 150 ms, the others
step once per press. Buttons 4 and 5 together return to zero. The offset is
limited to ±96 ticks and survives a change of selector. Tick generation and
the MIDI clock never move: only the player does.
- **No setting is written to flash while the board plays**, because a flash
  write stops both cores and would break the clock. Settings changed with the
  knobs live until power-off; the web loader writes the stored copy.

#### Sample change glide

A newly selected sample enters on the next bar line, and the tempo glide
starts on the same tick:

- `tempo(k) = T0 × (T1 / T0) ^ smootherstep(k / N)`, with
  `smootherstep(t) = 6t⁵ − 15t⁴ + 10t³`.
- `N` is 192 ticks (2 bars) unless the largest per-tick step,
  `|ln(T1 / T0)| × 1.875 / N`, exceeds 0.0025; then it is 384 ticks (4 bars),
  which is the longest glide there is.
- The glide lands exactly on the target tempo at tick `N`.
- Playback speed is interpolated between ticks, once per audio block, so it
  slides instead of stepping.
- Selecting another sample mid-glide starts a new glide from the tempo of the
  moment, again on the next bar line. A sample whose BPM already matches does
  not glide at all.
- Tunnel jumps never change the tempo: the jumped-to sample is varispeeded to
  the running tempo instead.

### MIDI out

31 250 baud 8N1 on **GPIO 22**, driven by PIO (the hardware UART TX pins are
taken by the LEDs and the knobs) at 12 mA. Only MIDI clock leaves the port:
one `0xF8` per tick, from boot onwards, without interruption. No transport
message is ever sent, and a byte is never allowed to delay a tick.

TRS Type A wiring:

| TRS  | MIDI DIN | Connection            |
| ---- | -------- | --------------------- |
| Tip  | pin 5    | GPIO 22 through 10 Ω  |
| Ring | pin 4    | 3V3 through 33 Ω      |
| Sleeve | pin 2  | GND                   |

### Follower (`CLOCK_SOURCE_EXTERNAL`)

- **Clock in: GPIO 22.** Internal pull-up, falling edge.
- **Reset in: GPIO 21.** Internal pull-up, falling edge.

Both inputs expect an inverting NPN stage, one per jack:

```
jack tip --- 100k ---+--- base (2N3904)      emitter --- GND
                     |
                    100k to GND
                     |
                  1N4148, cathode to base, anode to GND

collector --- 1k --- GPIO (22 = clock, 21 = reset)
```

A rising edge at the jack is therefore a falling edge at the GPIO. Pulses
shorter than 300 µs apart are ignored, as are resets within 50 ms of each
other. Beats are eighth notes produced at the moment the marking pulse
arrives; the tempo estimate follows the incoming pulses, clamped to
30–300 BPM. When the clock stops, the slice in flight finishes and the beat
LED goes dark. **Restart from step 1 when the clock starts** (on by default)
makes the first pulse after a stop play the first beat again. A pulse on the
reset input restarts the pattern from the first beat: within 5 ms of a clock
pulse it belongs to that pulse and acts at once, otherwise it waits for the
next pulse. Divisions of 1, 2, 4, 8, 12, 24 and 48 PPQN are selectable in the
web loader (default 24, which matches the 1010music Blackbox analog clock
output). MIDI clock in is not supported.

GPIO 23 is driven permanently high (SMPS PWM mode) in both builds, so the
WS2812 output is disabled. There is no trigger output. GPIO 21 is the
start/stop button on the master build (a button to ground; the internal
pull-up does the rest) and the reset input on the follower build.

## Controls

The first knob picks a selector position; knob A and knob B then do what the
row says. On the master build the buttons nudge instead of firing slices while
selector 1 is chosen.

| Selector | Knob A                          | Knob B                      |
| -------- | ------------------------------- | --------------------------- |
| 1        | Sample select                   | **Tempo** (master) / Break amount (follower) |
| 2        | **DJ filter**: low-pass / bypass / high-pass | Timestretch    |
| 3        | Noise gate threshold            | Gate probability            |
| 4        | Jump probability                | Retrigger probability       |
| 5        | Tunnel probability              | Reverse probability         |
| 6        | **Looper** hold/fade (master) / Sequencer record (follower) | **Record velocity + erase** (master) / Sequencer play (follower) |
| 7        | **Macro intensity** (master) / Save (follower) | **Macro mode** (master) / Load (follower) |
| 8        | **Freeze** (master) / **Volume** (follower) | — (free)         |

**DJ filter (selector 2, knob A).** One pot sweeps a 4th-order Linkwitz-Riley
isolator. The middle of the pot (±5 %) is a hard bypass: audio passes through
bit for bit. Turning left sweeps a low-pass from 10 kHz down to 60 Hz; turning
right sweeps a high-pass from 30 Hz up to 6 kHz. There is no resonance, and
the cutoff is smoothed so a fast turn does not zipper.

**Volume (selector 8, knob A).** Follower build only: a linear gain, silent at
the bottom of the pot and unity at the top, with no distortion stage. The
master build has no volume control — the output runs at full level and only
the 5 ms mute ramp touches it, so set the level after the board.

## Repository layout

- `firmware/`: Pico firmware (C/C++, pico-sdk 2.1.1)
- `web/`: browser loader for firmware and samples (Vite + React)
- `.github/workflows/`: firmware CI and GitHub Pages deploy

## Sample BPM

Every sample needs a BPM, because the master build takes its tempo from it.
The loader works it out for a whole batch of dropped files at once:

1. A BPM in the file name is taken as given: `120bpm`, `bpm120`, `128 BPM`,
   `87.5bpm` (case-insensitive), or a leading number as sample packs write it
   (`180 Classic Amen`, `124-house`), accepted between 60 and 250 BPM.
2. Otherwise the length is divided into 4, 8, 16 and 32 beats for candidate
   tempos, and a spectral flux onset envelope plus its autocorrelation gives an
   approximate tempo. The candidate closest to it wins, counting half and
   double as equally close.
3. When a candidate and its double are equally plausible, the octave is settled
   from the batch: a sample whose length matches (or halves, or doubles) one
   that names its BPM (1 % tolerance) takes the candidate closest to that
   sibling's tempo — the value itself always comes from its own length.
   Failing that, the candidate with 2.5–3.5 onsets per beat, and failing that
   the one inside 80–180 BPM.

Each row shows where its BPM came from. The chosen tempo is checked against the
estimate allowing the half, double and three-against-two readings; rows are
marked when the BPM came from the onset-density or 80–180 rule, or when no
reading agrees within 4 %. A name or a length match is evidence enough to stay
unmarked. Play a marked row against the click to check it. The preview button plays the loop with a
click at its BPM, and the click can be switched off.

Samples are placed in slots slowest first by centi-BPM, ties by name; editing a
BPM and leaving the field re-sorts them. **Download named copies** hands back
the files you added as a zip with `_119.23bpm` appended to each name (files
that already state their BPM are left alone).

A tempo below 40 BPM is doubled, and one above 300 BPM halved, until it is
inside the range the firmware plays; such a row says "folded" next to where
its BPM came from. A tempo typed in by hand is folded the same way.

If no BPM can be found the field is left empty and the bank cannot be uploaded
until it is filled in; every row's BPM is editable to two decimals.

Bank format v3 stores `source_bpm` as centi-BPM in the same 16-bit field
(12000 = 120.00 BPM, up to 655.35), so fractional tempos survive the trip to
the board. The loader still reads v2 banks, where that field held whole BPM.
A v3 bank is rejected by firmware older than this change, and firmware from
this change on reports its sample count as 0 for a v2 bank already in flash:
re-upload the bank after updating.

## Build the firmware

Requires `cmake`, `arm-none-eabi-gcc` with newlib and `python3`
(`make prereqs` installs them).

```sh
cd firmware
make pico-sdk   # clones pico-sdk 2.1.1 into firmware/pico-sdk
make build
```

The output is `firmware/build/artificial-infinite-2mb.uf2`, which is also
copied to `web/public/`. Build options (LED, MIDI in, PCB v2 knob layout) are
set in `firmware/target_compile_definitions.cmake`.

## Web loader

```sh
cd web
npm install
npm run dev
```

The loader uses WebSerial, so it needs a Chromium-based browser. It downloads
the UF2 and can reboot a connected device into BOOTSEL mode, after which you
copy the UF2 to the `RPI-RP2` drive. It also manages the sample bank on the
device.

## Credits and license

Based on [pikocore](https://pikocore.com) by
[schollz / infinitedigits](https://github.com/schollz/pikocore).
Released under the MIT License; see [LICENSE](LICENSE).
