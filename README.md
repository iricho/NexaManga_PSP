# NexaManga PSP

NexaManga PSP is a from-scratch manga/comic reader designed around the Sony PSP's 480x272
screen, controller, and limited memory. The original manga images remain authoritative:
the reader keeps source spatial resolution and builds only a screen-sized RGB565
viewport for fit, zoom, and pan.

This repository is under active development. Folder and CBZ JPG/JPEG/PNG reading,
progress/settings persistence, a bounded decoded-page cache, cooperative neighbor
preloading, direct scanline JPEG-to-RGB565 decoding, a cover-grid library, Smart
Reading, split spreads, and bookmarks are implemented. CBR, tiled decoding, filters,
and panel vision are not implemented.

## Supported library layouts

Normal chapter folders:

```text
ms0:/MANGA/
    Bleach/
        cover.jpg
        Chapter 001/
            001.jpg
            002.jpg
        Chapter 002/
            001.png
```

Single-folder manga:

```text
ms0:/MANGA/
    OneShot/
        001.jpg
        002.jpg
```

Folder and CBZ chapters may be mixed in one series:

```text
ms0:/MANGA/
    Berserk/
        Chapter 001/
            001.jpg
        Chapter 002.cbz
        Chapter 003.CBZ
```

Roots are checked in this order:

- PSP: `ms0:/MANGA`, then `ef0:/MANGA`, then `./MANGA`
- desktop: `./MANGA`

Series, folder/CBZ chapters, and pages use natural numeric sorting. CBZ display names
omit the extension, while the archive path remains the stable progress identity.
Hidden/system entries and junk archive paths such as `__MACOSX` are ignored. Duplicate
archive entry names are filtered by a normalized, case-insensitive key. Supported page
extensions are `.jpg`, `.jpeg`, and `.png`, case-insensitively.
`cover.*` and `folder.*` are detected as cover candidates; when absent, the first
naturally sorted folder or CBZ page becomes fallback cover metadata. The 4-column
library uses a bounded RGB565 thumbnail LRU. JPEG covers use disposable 1/8 previews
(or 1/4 when needed), and visible covers are not repeatedly decoded.

## Controls

### PSP library and series screens

- D-pad: select Continue, a cover, or a chapter
- Cross: open/read (resumes saved page when available)
- Circle: back; from the library it closes the application
- L/R on library: page four covers at a time
- Start: validated Continue Reading with recovery
- Square on library: cycle Recent, Alphabetical, Progress, and Favorites sorting
- Triangle: open Options for themes, HUD timing, default direction, and About

### PSP reader

- L / R: backward / forward logical reading position (buttons never reverse)
- Cross: deterministic Smart Zoom
- Triangle: zoom out
- Square: toggle Fit Page / Fit Width
- D-pad: precise stepped pan
- Analog stick: proportional smooth pan
- Select: reader menu for direction, Smart Reading, spreads, fit, themes, bookmarks,
  chapters, and the debug overlay
- Circle: save and return

The reader HUD disappears after the configured timeout and reappears on input. The
page remains visible beneath the translucent controller-first reader menu.

## Themes and frontend

The controller-first 480x272 frontend uses one lightweight palette system across the
cover-grid library, Continue Reading card, series page, menus, bookmarks, and reader
HUD. Built-in themes are Nexa, Manga Mono, Crimson, Midnight Blue, and Paper. Select a
theme from **Options > Theme** or from the reader menu; the preview applies immediately
and does not rebuild the library, thumbnails, page textures, or cache.

## Reading direction, Smart Reading, and spreads

Direction controls content order: RTL starts at the rightmost horizontal viewport and
LTR at the leftmost. In both modes R moves forward and L backward. Smart Reading
derives viewport stops from source dimensions, current zoom, the 480x272 viewport, and
15% overlap. It covers each column top-to-bottom and resynchronizes to the nearest stop
after manual panning.

Wide-page detection uses a tolerant landscape range plus plausible half-page aspect
ratios. Auto splits likely spreads; Full shows the physical page; Split treats both
halves as logical positions over one decoded surface. RTL reads right then left, LTR
left then right. Progress remains keyed to the physical page.

### Desktop keyboard

- Arrow keys: menu movement / precise pan
- W/A/S/D: analog-style pan
- X or Enter: Cross
- C or Escape: Circle
- Q / E: L / R
- T: Triangle
- F: Square
- P: Start
- Tab: Select
- F3: toggle and persist the debug memory overlay

Close the desktop window to quit.

## Persistence

`mangapsp-progress.dat` and `mangapsp-settings.dat` remain stored in the application
working directory. Both formats are versioned. Writes go through a temporary file and
backup/replace sequence. A malformed progress or bookmark row is skipped without
destroying valid entries. Version 1 progress and version 1-3 settings load with safe
defaults. Settings are written as version 4 with the selected theme; older settings
migrate to Nexa without resetting other values. Progress is checkpointed on
logical/page changes, preference changes, reader exit,
suspend, and application exit—not every frame.

Continue Reading restores a validated series, chapter, clamped physical page, fit,
direction, Smart Reading, spread mode, and logical position. If a chapter disappears,
it recovers within the same series. Bookmarks store stable paths with name fallback,
physical page, logical position, label, and monotonic order. Completion is set only by
normal forward navigation reaching the final readable page.

## PSPDEV dependencies

Install a current [PSPDEV toolchain](https://pspdev.github.io/installation.html), then
install the reader libraries:

```bash
psp-pacman -Sy sdl2 sdl2-image sdl2-gfx minizip jpeg
```

Set the SDK environment:

```bash
export PSPDEV="$HOME/pspdev"
export PATH="$PSPDEV/bin:$PATH"
```

The toolchain's `pspdev.cmake` selects `psp-g++`, configures `psp-pkg-config`, defines
`PSP`, and provides `create_pbp_file()`.

## Build for PSP

```bash
cmake -S . -B build-psp \
  -DCMAKE_TOOLCHAIN_FILE="$PSPDEV/psp/share/pspdev.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF
cmake --build build-psp
```

`psp-cmake` may be used as the configure wrapper instead:

```bash
mkdir build-psp
cd build-psp
psp-cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF ..
cmake --build .
```

For the optimized RC profile, add `-DMANGAPSP_PSP_RC=ON`. The branded package is copied
to `dist/psp-rc/NexaMangaPSP/EBOOT.PBP`. New installs may place it at:

```text
ms0:/PSP/GAME/NexaMangaPSP/EBOOT.PBP
```

The XMB title is `NexaManga PSP`. Packaging automatically uses `ICON0.PNG` when present,
falls back to the existing `Icon.png`, and remains valid when neither icon is available.
For an existing MangaPSP installation, replace the EBOOT in its existing PSP/GAME
directory so the relative `mangapsp-progress.dat` and `mangapsp-settings.dat` files stay
in place. If moving to the new `NexaMangaPSP` directory name, copy both `.dat` files with
the EBOOT. Their names and formats intentionally remain compatible.

## Desktop build

Desktop development is primarily supported on Linux or WSL with SDL2 development
packages and pkg-config installed. For Debian/Ubuntu:

```bash
sudo apt install build-essential cmake pkg-config \
  libsdl2-dev libsdl2-image-dev libsdl2-gfx-dev libjpeg-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DMANGAPSP_FETCH_MINIZIP=ON -DMANGAPSP_FETCH_JPEG=ON
cmake --build build
./build/MangaPSP
```

`MANGAPSP_FETCH_MINIZIP=ON` fetches pinned minizip-ng 3.0.4 and zlib 1.3.1;
`MANGAPSP_FETCH_JPEG=ON` builds pinned libjpeg-turbo 3.1.3 as an external static
dependency. With them disabled, pkg-config must expose minizip-ng as `minizip` and
CMake must find a compatible JPEG library. The older zlib-contrib minizip API is not
compatible.

To compile and run the core tests (no SDL packages required):

```bash
cmake -S . -B build-core -DMANGAPSP_BUILD_APP=OFF \
  -DMANGAPSP_FETCH_MINIZIP=ON -DMANGAPSP_FETCH_JPEG=ON -DBUILD_TESTING=ON
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

Set `MANGAPSP_BUILD_BENCHMARKS=ON` to build the synthetic direct-JPEG development
benchmark. Its old-path column is a byte-model comparison, not measured SDL_image RSS.

## Image and memory behavior

Decoded resident pages are RGB565 at their original width and height. Zoomed views
therefore resample source detail rather than enlarging a permanent 480x272 copy. A
480x272 RGB565 CPU viewport and streaming texture add roughly 510 KiB combined.

The cache is limited to the current page and its immediate neighbors. It charges all
resident decoded pages plus the viewport and estimated texture against one configurable
budget. Required navigation is transactional: a failed, corrupt, or over-budget load
leaves the current page visible. During idle opportunities the reader attempts N+1 and
then N-1, at most one synchronous decode per opportunity; preload failures do not mark
a page unreadable and can be retried by normal navigation.

JPEG is no longer routed through SDL_image. The loader probes the header first, checks
the predicted peak, allocates the final source-resolution RGB565 surface, and asks
libjpeg to decode one grayscale or RGB scanline at a time directly into it. Folder JPEGs
stream through `FILE*`; CBZ JPEGs decode from the selected in-memory entry without a
temporary file. The selected CBZ bytes are released when the transactional load returns.
PNG remains isolated behind a probed SDL_image fallback and therefore retains the older
full-decode-plus-RGB565 transient cost.

Accounting separates bytes NexaManga PSP can track from approximate libjpeg-owned memory.
Tracked JPEG decode bytes are retained cache/viewport/texture state, the final RGB565
surface, one scanline, and selected compressed CBZ bytes (zero for folder pages).
Predicted peak adds a conservative decoder allowance and progressive coefficient
storage estimate. The debug overlay shows current/previous/next pages, viewport and
texture estimate, CBZ bytes, scanline bytes, estimated library overhead, tracked and
predicted peaks, source/probe/decode/output timings, and decode/cache counters.

A 1600x2400 RGB565 page is exactly 7,680,000 bytes, or 7.32421875 MiB, before allocator
metadata. The old JPEG path could simultaneously hold a full RGB24/RGBA decode plus
that RGB565 replacement; the direct path instead adds only a scanline and libjpeg's
internal working state. Source dimensions are never silently reduced. Full-detail pages
that fail the budget are rejected transactionally, leaving the current page visible.
Native JPEG 1/2, 1/4, and 1/8 DCT scaling is represented explicitly for disposable
previews. The reader caches authoritative full detail; the separate cover cache keeps
only scaled previews and is bounded to roughly 2 MiB.

See [AUDIT.md](AUDIT.md) for findings, build results, memory analysis, and hardware
acceptance tests. See [ARCHITECTURE.md](ARCHITECTURE.md) for component boundaries.

## Current limitations and next phase

- No CBR support; CBZ is the archive format implemented in this phase.
- No CBR, filters, panel metadata, or panel mode yet.
- Thumbnail caching is memory-only and disposable; a disk cache remains optional.
- Suspend/resume callbacks are implemented but require real-hardware acceptance tests.
- Unicode paths are preserved as byte strings, but actual PSP filesystem behavior must
  be verified on hardware.

The hardware acceptance and release-candidate procedure is in
[`PSP_HARDWARE_TEST.md`](PSP_HARDWARE_TEST.md). It includes current PSPDEV package
preflight, reproducible Development and Release PBP commands, conservative 32/64
MiB memory profiles, synthetic test-library generation, storage paths, and the
physical-device evidence gate.

The startup-crash smoke target, numbered boot trace, subsystem-isolation switches, and
minimal physical bisection order are documented in
[`PSP_STARTUP_DIAGNOSTICS.md`](PSP_STARTUP_DIAGNOSTICS.md).

The immediate gate is executing that matrix on physical 32 MiB and expanded-memory
hardware. Region/tiled rendering remains a separate future decision for full-detail
pages that cannot coexist with the current transactional page.
