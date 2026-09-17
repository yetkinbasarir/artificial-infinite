# Artificial Infinite

[![firmware](https://github.com/yetkinbasarir/artificial-infinite/actions/workflows/firmware.yml/badge.svg)](https://github.com/yetkinbasarir/artificial-infinite/actions/workflows/firmware.yml)

Artificial Infinite is a lo-fi sample mangler firmware based on
[schollz/pikocore](https://github.com/schollz/pikocore), trimmed down to target
**only the 2 MB Raspberry Pi Pico**. Firmware behaviour is identical to the
upstream 2 MB build, except that the boot2 flash SPI clock divider is raised to
4; see [UPSTREAM.md](UPSTREAM.md) for details.

## Flash layout (2 MB)

| Offset      | Size      | Contents                                                        |
| ----------- | --------- | --------------------------------------------------------------- |
| `0x000000`  | 512 KB    | Firmware reserve (`PIKO_FIRMWARE_RESERVE`)                      |
| `0x07F000`  | 4 KB      | Settings (last sector of the firmware reserve)                  |
| `0x080000`  | 12 KB     | Sample bank header (v2, up to 128 samples)                      |
| `0x083000`  | ~1.56 MB  | Audio: 1,560,576 bytes ≈ 65 s of 8-bit audio at 24 kHz          |

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
