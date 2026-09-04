# PSP startup diagnostics

This phase does not claim that the physical-hardware crash is fixed. It turns startup
into a persistent, numbered trace so the failing boundary can be identified without
changing reader behavior in Release.

## Startup order and risk ranking

The current real-app order is:

1. `main`, boot-log setup, `App` construction (`Input` configures PSP controls)
2. settings decision/load
3. SDL, SDL_image, window, accelerated renderer with software fallback
4. memory classification and bounded image-budget selection
5. `MangaReader` viewport surface and RGB565 streaming texture
6. optional thumbnail-cache object and lifecycle callback thread
7. manga-root probing, synchronous folder/CBZ library scan, progress load
8. first event poll and PSP controller poll
9. first render (including visible-cover thumbnail generation when enabled), present,
   normal loop

Ranked physical-PSP suspects are:

1. A normal initialization failure was previously visible only on stderr and returned
   to SDL2main, which can look exactly like a black-screen crash on retail hardware.
2. The first real UI frame uses SDL2_gfx text drawing before the first present; the
   smoke binary deliberately avoids SDL2_gfx.
3. A non-empty library may decode and create a cover texture before the first present.
4. CBZ discovery invokes the locally repaired minizip package during the synchronous
   startup scan.
5. `MangaReader` creates its 480x272 RGB565 streaming texture before the first frame.
6. The PSP lifecycle callback thread is installed before scanning/rendering.
7. SDL_image JPG+PNG initialization or renderer creation may fail cleanly but was
   previously unobservable.

Missing `ms0:/MANGA`, missing `ef0:/MANGA`, and an empty directory are handled as an
empty library. The cache budget is not preallocated: 16 MiB and 36 MiB are upper
bounds. Reader startup creates only the existing 480x272 RGB565 CPU surface and
streaming texture (about 510 KiB combined).

The installed current PSPSDK libc glue treats any negative `PSP_HEAP_SIZE_KB` value as
"allocate the largest free block" and takes the held-back amount from the separate
heap-threshold symbol, defaulting to 512 KiB. The former `PSP_HEAP_SIZE_KB(-1024)` was
therefore not holding back 1024 KiB. Both PSP executables now express the intended
policy as `PSP_HEAP_SIZE_KB(-1)` plus `PSP_HEAP_THRESHOLD_SIZE_KB(1024)`. PBP
`MEMSIZE=1` remains unchanged; the installed current `CreatePBP.cmake` defines it as
access to all available memory. SDL2main remains the sole owner of module metadata.

## Boot stages

The development app writes `mangapsp-boot.log` beside the EBOOT when the executable
path is available, then falls back to the process working directory. Every diagnostic
record is appended, flushed, and closed before startup continues. Closing each record
is required for crash durability on PSP Memory Stick storage; `fflush()` alone left a
zero-byte file after the first physical fine-diagnostic run powered off.

| Stage | Reached after |
|---:|---|
| 01 | `main` entered and boot log opened |
| 02 | `App` constructed |
| 03 | settings/persistence decision applied |
| 04 | SDL initialized |
| 05 | SDL_image JPG+PNG initialized |
| 06 | window created |
| 07 | renderer created |
| 08 | memory profile and image budget selected |
| 09 | reader and viewport resources ready |
| 10 | thumbnail decision applied |
| 11 | lifecycle callbacks attempted |
| 12 | manga root selected |
| 13 | library scan/progress decision complete |
| 14 | first event/input poll completes and first real frame begins |
| 15 | first real frame presented |
| 16 | normal run loop remains alive |

The stage-03-to-04 interval is further divided as follows:

| Checkpoint | Exact meaning |
|---:|---|
| 03.1 | the disabled stage-03 hold gate returned |
| 03.2 | immediately before combining constant SDL initialization flags |
| 03.3 | flags recorded; immediately before entering `SDL_Init` |
| 03.4 | `SDL_Init` returned; its numeric result follows in the log |
| 03.5 | nonzero-result branch, immediately before `SDL_GetError` |
| 03.6 | `SDL_GetError` returned and its text follows in the log |
| 03.7 | zero-result branch, immediately before committing stage 04 |

`MANGAPSP_BOOT_HOLD_CHECKPOINT=33` intentionally holds before `SDL_Init`, while `34`
holds immediately after it returns. `MANGAPSP_DEFER_PSP_INPUT_SETUP=ON` is a
development-only comparison that moves the two `sceCtrlSetSampling*` calls from the
`Input` constructor to immediately after stage 04; it does not affect Release unless
explicitly enabled in a Debug configuration.

The log also records SDL driver/renderer, isolation switches, memory profile, measured
free user bytes, selected budget, root, series count, and initialization errors. Stage
01 is visible through the PSPSDK debug screen. Selected post-renderer holds draw a raw
SDL color pattern and remain alive. Fatal initialization failures show their last stage
and log path and remain visible.

The PSPSDK exception installer is intentionally not registered. In the installed SDK
it imports `sceKernelRegisterDefaultExceptionHandler` from `libpspkernel`, and the
official exception sample declares a kernel module. NexaManga PSP is an SDL2main-owned
user-mode module, so installing it would require an unsafe mode/linkage change. The
last flushed stage is the safe crash evidence for this build.

## Build-time isolation controls

All isolation behavior is additionally guarded by `MANGAPSP_DEVELOPMENT`, so setting
one accidentally in Release does not disable a subsystem.

- `MANGAPSP_BOOT_DIAGNOSTICS=ON`: persistent stage trace and visible failure/holds.
- `MANGAPSP_BOOT_HOLD_STAGE=0..16`: hold at one numbered boundary; 0 continues.
- `MANGAPSP_DISABLE_CBZ=ON`: skips CBZ discovery/inspection/use during scanning.
- `MANGAPSP_DISABLE_THUMBNAILS=ON`: does not create/use the thumbnail cache; existing
  placeholder covers remain.
- `MANGAPSP_IGNORE_PERSISTENCE=ON`: skips settings/progress reads and all writes;
  existing files are not changed.
- `MANGAPSP_FORCE_NO_MANGA=ON`: skips root probing and scanning entirely.

One source tree covers the requested A-K bisection:

| Group | Configuration/boundary |
|---|---|
| A | standalone `MangaPSP_PSP_Smoke` |
| B | hold 02 (`App` shell and PSP input constructor) |
| C | hold 08 (SDL plus memory profile) |
| D | hold 03 with persistence normal/ignored comparison |
| E | hold 09 (reader/image viewport resources) |
| F | hold 12 (root resolution) |
| G | hold 13 with CBZ and thumbnails disabled, folder scan enabled |
| H | hold 13 with CBZ enabled |
| I | stage 15 comparison with thumbnails disabled/enabled |
| J | stage 15 proves the first actual library render/present |
| K | stage 16 proves the normal run loop |

Example stage-7 diagnostic configure:

```bash
psp-cmake -S . -B build-psp-hold-7 \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF \
  -DMANGAPSP_BOOT_DIAGNOSTICS=ON \
  -DMANGAPSP_BOOT_HOLD_STAGE=7
cmake --build build-psp-hold-7
```

## Minimal physical test sequence

Use a different `ms0:/PSP/GAME/` directory name for each build, or replace and verify
the timestamp/size before every launch.

The SDL smoke test has passed on a PSP-3000. Do not proceed to manga, thumbnails, CBZ,
or Release until this replacement stage-03 sequence resolves the boundary:

1. `dist/psp-debug-03-hold/MangaPSP/EBOOT.PBP`: must remain at the intentional stage-03
   hold.
2. `dist/psp-debug-03-hold-before-sdl/MangaPSP/EBOOT.PBP`: must show/flush 03.3 and
   remain at the intentional pre-`SDL_Init` hold.
3. `dist/psp-debug-03-hold-after-sdl/MangaPSP/EBOOT.PBP`: if `SDL_Init` returns, must
   show/flush 03.4, log `sdl_init_result=0`, and remain at the intentional hold.
4. `dist/psp-debug-03-fine/MangaPSP/EBOOT.PBP`: continues with the original controller
   ordering and records every 03.x boundary.
5. Only if a preceding log stops at 03.3 inside `SDL_Init`, run
   `dist/psp-debug-03-fine-input-deferred/MangaPSP/EBOOT.PBP`. It differs only by
   performing PSP controller sampling setup after stage 04.

Retrieve `mangapsp-boot.log` before the next launch because each diagnostic run opens
its own log with truncation. Report the exact final line, last visible checkpoint,
whether the PSP powered off or returned to XMB, and approximate elapsed time.

After the zero-byte-log result, use the replacement single-pass artifact at
`dist/psp-debug-03-one-test/MangaPSP/EBOOT.PBP`. It also prints stage 04 and all later
numbered stages to the PSP debug screen; it retains all four isolation switches and
contains no hold.

## Physical result: stage 03 through stage 07

The replacement single-pass log was recovered successfully from a PSP-3000. It proves
that `SDL_Init` returned zero and that stages 04 through 07 completed. The final durable
record was `STAGE 07 renderer created`. Because that build printed the stage label after
writing the record, its next operation was a `pspDebugScreenPrintf` after SDL had taken
ownership of the renderer/framebuffer. Post-SDL debug-screen output is therefore removed
from the next build; stages above 03 remain durable-log-only.

Use `dist/psp-debug-07-one-test/MangaPSP/EBOOT.PBP` for the next single run. Its log-only
checkpoints distinguish the remaining stage-07-to-stage-08 interval:

| Checkpoint | Meaning |
|---|---|
| 07.1 | Disabled stage-07 hold gate returned |
| 07.2 | Immediately before `SDL_SetRenderDrawBlendMode` |
| 07.3 | Blend-mode call returned; `sdl_blend_result` follows |
| 07.4-07.6 | Failure-only `SDL_GetError` and warning path |
| 07.7 | Before reading the configured image budget |
| 07.8 | Immediately before budget clamp and the first PSP free-memory query |
| 07.9 | Budget clamp/free-memory query returned |
| 07.10 | Immediately before `hardwareInfo()` and its current free-memory query |
| 07.11 | `hardwareInfo()` returned |
| 07.12 | Memory notes completed; immediately before stage 08 |

## Physical result: defective free-memory helper

The PSP-3000 log reached 07.8 and stopped inside the first memory query. The installed
`libpspsdk.a` implementation of `pspSdkTotalFreeUserMemSize()` was then disassembled:
it calls `pspSdkDisableInterrupts()` and branches to itself indefinitely. The hardware
watchdog therefore powers the PSP off. This is the definite startup bug.

MangaPSP no longer references that helper. `Platform::captureEarlyMemoryProfile()` is
the first statement in `SDL_main` and captures `sceKernelMaxFreeMemSize()` before boot
logging or app construction can cause newlib to reserve its heap. PSPSDK's own `_sbrk`
implementation uses the same user-mode query to size its heap. The captured largest
free block drives the existing 32/64 MiB classification and is later logged as
`startup_max_free_block_bytes`.

Use `dist/psp-debug-memory-fix-one-test/MangaPSP/EBOOT.PBP` for the single verification
run. The executable has no `pspSdkTotalFreeUserMemSize` symbol, retains all isolation
switches, and contains no hold.

## Physical result: isolated startup passed

The memory-fix diagnostic reached and remained in the normal empty library UI on the
PSP-3000. This resolves the isolated startup crash and permits the next test to enable
folder scanning only. CBZ, thumbnails, and persistence remain isolated.

## Folder-manga-only hardware test

The PSP root candidates remain `ms0:/MANGA`, `ef0:/MANGA`, and `./MANGA`, in that
order. The expected fixture is `ms0:/MANGA/BLEACH/13/0000.jpg` through `0003.jpg`.
Folder scanning accepts JPG, JPEG, and PNG case-insensitively and naturally sorts the
four page names.

The normal build is:

`dist/psp-debug-folder-only/MangaPSP/EBOOT.PBP`

The scan-only hold build is:

`dist/psp-debug-folder-only-hold/MangaPSP/EBOOT.PBP`

Both have boot diagnostics and manga scanning enabled, while CBZ, thumbnails, and
persistence are disabled. CBZ/minizip is compiled out rather than merely bypassed.
The hold build resolves and scans the root before constructing `MangaReader` or
`ThumbnailCache`, then displays:

```text
Folder scan complete
Series count: X
Root: ms0:/MANGA
```

Run the hold build first. A correct fixture produces `Series count: 1`. Then replace
it with the normal build, verify BLEACH, open chapter 13, and navigate pages 0000
through 0003. The durable log records `root_candidate`, `root_candidate_exists`,
`root_selected` (or `root_fallback`), `root`, `root_exists`, `series_candidate`,
`chapter_candidate`, `chapter_pages`, each naturally ordered `page_candidate`,
`scan_series_count`, and the app-level `series_count`. Failures additionally record
`scan_error`, `scan_failure`, `series_skipped`, or `chapter_skipped` with a reason.

## Physical result: folder manga passed

The PSP-3000 folder-only build detected and opened `ms0:/MANGA/BLEACH/13` on real
hardware. Startup, root selection, folder discovery, chapter discovery, natural page
ordering, and the folder-backed reader path are therefore accepted for this device.

The next combined-feature artifact is `dist/psp-rc/NexaMangaPSP/EBOOT.PBP`. It is an
optimized `MANGAPSP_PSP_RC=ON` build with CBZ, thumbnails, persistence, lifecycle,
reader, and cache behavior enabled together. It retains flushed major stages and
durable runtime errors but contains no stage/checkpoint hold strings, no forced-empty
library path, and no verbose page-cache logging.
