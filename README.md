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
| `0x080000`  | 12 KB     | Sample bank header (v2, up to 128 samples)                      |
| `0x083000`  | ~1.56 MB  | Audio: 1,560,576 bytes ≈ 65 s of 8-bit audio at 24 kHz          |

## External clock

Artificial Infinite has no internal tempo: it follows an external analog pulse
clock, so several boards fed from the same clock stay aligned.

- **Clock in: GPIO 22.** Internal pull-up, falling edge.
- **Reset in: GPIO 21.** Internal pull-up, falling edge.
- **GPIO 23** is driven permanently high (SMPS PWM mode), so the WS2812 output
  is disabled. There is no trigger output.

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
other.

### Behaviour

- One beat is an eighth note, produced at the moment the marking pulse
  arrives. Nothing is predicted or snapped to a grid.
- The tempo estimate follows the incoming pulses and drives sample playback,
  clamped to 30–300 BPM.
- When the clock stops, the slice in flight finishes its own length and the
  beat LED goes dark. The position is kept.
- **Restart from step 1 when the clock starts** (on by default, switchable in
  the web loader) makes the first pulse after a stop play the first beat again.
  Turn it off to continue from where the clock stopped.
- A pulse on the reset input restarts the pattern from the first beat. A reset
  within 5 ms of a clock pulse belongs to that pulse and acts immediately;
  otherwise it waits for the next pulse, including while the clock is stopped.

### Pulse divisions

1, 2, 4, 8, 12, 24 and 48 PPQN, set in the web loader; the default is 24 PPQN.
That matches the 1010music Blackbox analog clock output. 2 PPQN suits the
Korg SQ-1 and Volca series, and Arturia BeatStep Pro or Moog DFAM clock
outputs work at whichever division they are set to. MIDI clock is not
supported: the clock input is analog only.

## Controls

The first knob picks a selector position; knob A and knob B then do what the
row says.

| Selector | Knob A                          | Knob B                      |
| -------- | ------------------------------- | --------------------------- |
| 1        | Sample select                   | Break amount (probabilities)|
| 2        | **DJ filter**: low-pass / bypass / high-pass | Timestretch    |
| 3        | Noise gate threshold            | Gate probability            |
| 4        | Jump probability                | Retrigger probability       |
| 5        | Tunnel probability              | Reverse probability         |
| 6        | Sequencer record                | Sequencer play              |
| 7        | Save settings                   | Load settings               |
| 8        | **Volume**                      | — (free)                    |

**DJ filter (selector 2, knob A).** One pot sweeps a 4th-order Linkwitz-Riley
isolator. The middle of the pot (±5 %) is a hard bypass: audio passes through
bit for bit. Turning left sweeps a low-pass from 10 kHz down to 60 Hz; turning
right sweeps a high-pass from 30 Hz up to 6 kHz. There is no resonance, and
the cutoff is smoothed so a fast turn does not zipper.

**Volume (selector 8, knob A).** A linear gain: silent at the bottom of the
pot, unity at the top. There is no distortion stage.

## Repository layout

- `firmware/`: Pico firmware (C/C++, pico-sdk 2.1.1)
- `web/`: browser loader for firmware and samples (Vite + React)
- `.github/workflows/`: firmware CI and GitHub Pages deploy

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
