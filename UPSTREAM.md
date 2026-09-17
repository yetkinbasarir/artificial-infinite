# Upstream

- **Source:** https://github.com/schollz/pikocore
- **Commit:** `1af735e` (`1af735ed294ac50db24c54e7f5e26601e3dc3aa5`, "feat: improve clock sync")
- **Imported:** 2026-09-17
- **License:** MIT (upstream `LICENSE`, unchanged)

The first commit in this repository is an unmodified snapshot of upstream at
that commit. The second commit restructures it into a 2 MB-only base.

**Firmware behaviour is identical to the upstream 2 MB build**
(`pikocore_2mb`, `PICO_FLASH_SIZE_BYTES=2097152`). Audio engine, clock, USB
protocol and descriptors, `USBD_MANUFACTURER`/`USBD_PRODUCT` strings, the
`PIKO1 ...` INFO reply, bank format (v2, 12288-byte header, 128 samples,
24 kHz) and `PIKO_FIRMWARE_RESERVE=524288` are unchanged. Known upstream bugs
were intentionally not fixed.

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
