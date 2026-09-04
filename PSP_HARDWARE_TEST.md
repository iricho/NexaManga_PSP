# NexaManga PSP Hardware Acceptance

This is the release-candidate acceptance procedure. Record observed values; do not
turn unmeasured desktop results into PSP pass/fail thresholds.

## 1. Toolchain and package preflight

Use a current PSPDEV/PSPSDK environment. The project consumes the package names
exported by the PSPDEV package repository: `sdl2`, `SDL2_image`, `SDL2_gfx`,
`libjpeg`, and minizip-ng's `minizip`. Minizip and SDL2_image bring `zlib` and
their image-format libraries through pkg-config.

```sh
psp-g++ --version
psp-cmake --version
psp-pkg-config --modversion sdl2 SDL2_image SDL2_gfx libjpeg minizip zlib
```

Configuration must stop with a useful error if any required package is absent.
Do not use desktop headers or libraries to bypass the PSP package check.

## 2. Reproducible PSP builds

From a clean build directory:

```sh
mkdir build-psp-release
cd build-psp-release
psp-cmake -DCMAKE_BUILD_TYPE=Release -DMANGAPSP_PSP_MEMORY_PROFILE=AUTO ..
cmake --build . --parallel
```

Expected artifact: `build-psp-release/EBOOT.PBP`. Release strips the ELF while
creating the PBP, disables the debug/hardware overlay, disables decode timers and
aggregate diagnostic metrics, and emits only minimal error logging.

For the optimized release candidate with all normal features and lightweight
observability:

```sh
mkdir build-psp-rc
cd build-psp-rc
psp-cmake -DCMAKE_BUILD_TYPE=Release -DMANGAPSP_PSP_RC=ON \
  -DMANGAPSP_PSP_MEMORY_PROFILE=AUTO ..
cmake --build . --parallel
```

Expected artifact: `build-psp-rc/EBOOT.PBP`. The RC profile forces boot tracing on;
forces every hold, no-manga, no-persistence, no-thumbnail, and no-CBZ isolation
switch off; uses the installed PSP minizip package; retains the metrics-backed debug
and Hardware Test overlays; and keeps normal Release optimization/stripping. Major
startup stages plus runtime archive, decoder, thumbnail, save, and lifecycle failures
are flushed to `mangapsp-boot.log`. Fine diagnostic checkpoints and per-page cache
hit/miss logging are not included. `ICON0.PNG` is used when present, with `Icon.png`
as a compatible fallback; packaging also succeeds without either optional icon.
The same RC EBOOT is packaged as `dist/psp-rc/NexaMangaPSP/EBOOT.PBP` with the XMB
title `NexaManga PSP`.

For a hardware-instrumented development build:

```sh
mkdir build-psp-development
cd build-psp-development
psp-cmake -DCMAKE_BUILD_TYPE=Debug -DMANGAPSP_PSP_MEMORY_PROFILE=AUTO ..
cmake --build . --parallel
```

Expected artifact: `build-psp-development/EBOOT.PBP`. Development enables logs,
decode/cache/timing metrics, the compact reader overlay, and **Reader Menu >
Hardware Test**.

`MEMSIZE=1` is deliberately passed to `create_pbp_file`, allowing the app to use
the memory available on each PSP model. The runtime still enforces its own safe
profile. To validate profile behavior independently of automatic detection, build
with `MANGAPSP_PSP_MEMORY_PROFILE=PSP_1000` or `PSP_2000_PLUS`.

Install one build at a time:

- Memory Stick: `ms0:/PSP/GAME/NexaMangaPSP/EBOOT.PBP`
- PSP Go internal storage: `ef0:/PSP/GAME/NexaMangaPSP/EBOOT.PBP`

For an upgrade, replacing the EBOOT inside the existing `PSP/GAME/MangaPSP` directory
is the safest path because settings/progress are relative to that directory. To rename
the directory too, copy `mangapsp-progress.dat` and `mangapsp-settings.dat` into the new
directory; do not delete or regenerate them.

Place the generated test library at `ms0:/MANGA/Acceptance_Test_Library` or
`ef0:/MANGA/Acceptance_Test_Library`. The scanner also checks `./MANGA`.

## 3. Synthetic acceptance library

Install Pillow, then generate outside the source tree:

```sh
python tools/generate_psp_test_library.py --output /path/to/staging
```

The generator creates grayscale and RGB baseline/progressive JPEGs at 1200x1800,
1400x2100, 1600x2400, 1800x2700, and 2000x3000; selected PNG fallback
pages; nested and large CBZ archives; corrupt/truncated images and archives; and a
many-chapter/long-metadata series. `--quick` is only a generator smoke test and is
not valid performance input.

## 4. Per-device record

Complete one row per physical device and build. Use the Hardware Test screen for
measured values.

| Field | Recorded value |
|---|---|
| PSP model / motherboard label | |
| Firmware / launcher | |
| Storage (`ms0`, `ef0`, both) | |
| Build commit / archive ID | |
| Build type and forced/auto profile | |
| Reported memory profile | |
| Free user memory at reader test | |
| Image budget | |
| Test page and source type | |
| Decode read / probe / decode / RGB565 / total µs | |
| Library-open / chapter-open µs | |
| Cold folder / cached next / uncached next µs | |
| CBZ extract+decode / thumbnail generation µs | |
| Page-switch µs | |
| Frame µs / FPS while idle, panning, menu open | |
| Cache hit/miss/eviction and resident bytes | |
| Thumbnail generation last/peak µs | |
| Preload completion/attempt counts | |
| Suspend/resume counts | |

Do not declare a timing regression until the same library, device, storage, build
type, and profile have been compared.

### Category sign-off

| Category | Result / issue reference |
|---|---|
| BOOT | |
| LIBRARY | |
| FOLDER READING | |
| CBZ READING | |
| ZOOM/PAN | |
| SMART READING | |
| SPREADS | |
| BOOKMARKS | |
| SAVE/RESUME | |
| SUSPEND | |
| HOME EXIT | |
| MEMORY | |
| THUMBNAILS | |
| LONG SESSION | |
| ERROR HANDLING | |

## 5. Functional and endurance checklist

### Memory and decoding

- Run the entire resolution matrix on a PSP-1000 or a forced `PSP_1000` build.
- Confirm the screen reports the 32 MiB profile and a 16 MiB image/cache budget.
- Repeat on a PSP-2000/3000/Go; confirm automatic expanded-memory selection where
  available and a 36 MiB ceiling.
- Confirm oversized pages show a usable memory-budget error without resizing,
  corruption, a process exit, or loss of the last visible page.
- Navigate L/R from corrupt and truncated pages. Confirm unreadable pages are
  skipped where possible and Circle can always leave the reader.
- Exercise baseline/progressive grayscale and RGB JPEG plus PNG fallback.

### Cache, preload, and thumbnails

- Navigate N, N+1, N, N-1 repeatedly and record cache hit/miss/eviction changes.
- Stop input for at least one preload opportunity. Confirm at most one candidate is
  attempted at a time and the visible page is never evicted for preload.
- Rapidly page while preload candidates exist. Confirm no stale page appears.
- Scroll the cover library across multiple groups. Confirm no more than one new
  thumbnail decode occurs in a rendered frame and memory remains bounded.

### Controls and legibility

- Verify Cross/Confirm, Circle/Back, D-pad, L/R, Triangle, Square, Start, Select.
- Test analog panning around center and at full deflection. Tune
  `analog_dead_zone` (4..48 raw units) and `analog_sensitivity` (0.25..3.0) only
  from a copied settings file; retain the device/value in the test record.
- Inspect all library, chapter, reader, bookmark, error, and hardware-test text on
  the PSP LCD for clipping, contrast, and menu selection clarity.

### Suspend, resume, and exit (critical)

- Suspend while idle on a folder page; resume and navigate both directions.
- Suspend on a CBZ page after preload has occurred; resume and navigate. The CBZ
  handle must reopen and the visible decoded page must remain valid.
- Suspend immediately after a page switch, during library browsing, and with the
  reader menu open. Repeat at least 20 cycles and record the counters.
- Remove or rename the active CBZ while suspended (development test). Resume must
  show a recoverable source-reopen error; Circle must still leave the reader.
- Use Home/Exit from library and reader states. Relaunch and verify page, logical
  position, reading direction, smart/spread/fit settings, bookmarks, and last
  selection. Existing version-2 progress files must load unchanged.

### Paths and storage

- Test `ms0:/MANGA`, `ef0:/MANGA` on PSP Go, and `./MANGA` beside the EBOOT.
- Test absent, empty, read-only, and deeply nested library paths.
- Test folder and CBZ chapters on both internal storage and Memory Stick when the
  device supports both.

## 6. Release-candidate evidence

Archive the two CMake configure logs, build logs, PBP hashes/sizes, device records,
and any photographed errors. A release candidate is hardware-accepted only after
the 32 MiB profile, suspend/resume, recoverable failures, persistence, and exit
checklists have physical-device evidence. Desktop tests alone do not satisfy this
gate.
