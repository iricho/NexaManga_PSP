# NexaManga PSP repository audit

Audit date: 2026-08-25

Update, 2026-08-27: PSPDEV and physical PSP-3000 testing are now available. Folder and
CBZ reading, thumbnails, bookmarks, progress, Continue Reading, navigation, and the
reader/cache path have passed on that device. The current phase renames the product to
NexaManga PSP and confines changes to the frontend, theme palette, settings migration,
and PBP branding; the accepted image/archive/cache architecture remains unchanged.

## Low-memory JPEG phase update

The PageSource/PageCache follow-up replaced normal JPEG use of SDL_image with a direct
libjpeg-turbo 3.1.3 path. Headers are probed before the final allocation; grayscale or
RGB scanlines are packed directly into the only full-size RGB565 destination. Folder
files use `jpeg_stdio_src`, CBZ entries use `jpeg_mem_src`, and a setjmp-based error
manager converts warnings/fatal errors into transactional failures with cleanup. PNG
continues through a separately probed SDL_image fallback.

A disposable CMake 3.31.8/Zig 0.13.0 environment built Debug and Release core/test
targets with warnings enabled. Four CTest executables passed in each configuration,
including synthetic grayscale/RGB/odd-width/progressive/corrupt/truncated/folder/CBZ/
budget/cache cases. Changed SDL-facing translation units compiled against official SDL
development headers, but installed SDL link libraries were unavailable. The direct
benchmark completed for 1200x1800, 1600x2400, and 2000x3000. ASan/UBSan was attempted,
but this portable Zig-on-Windows configuration could not link its sanitizer runtimes.
PSPDEV remains unavailable, so there is still no PSP or EBOOT build claim.

The optimized synthetic benchmark produced the following desktop development snapshot:

| Synthetic RGB JPEG | Final RGB565 | Direct tracked | Direct predicted | Old RGB24 + RGB565 byte model | Direct decode |
|---|---:|---:|---:|---:|---:|
| 1200x1800 | 4.12 MiB | 4.37 MiB | 4.55 MiB | 10.54 MiB | 7.12 ms |
| 1600x2400 | 7.32 MiB | 7.76 MiB | 7.96 MiB | 18.74 MiB | 12.56 ms |
| 2000x3000 | 11.44 MiB | 12.13 MiB | 12.35 MiB | 29.29 MiB | 19.72 ms |

These are synthetic, machine-specific timings. The old column is a calculated RGB24
floor plus RGB565 and encoded bytes, not measured SDL_image RSS; an RGBA decode or
allocator overhead would be higher.

## Baseline repository

The starting repository contained a 1,393-line C++/SDL2 scaffold split across `App`,
`FileScanner`, `Input`, `MangaReader`, and two plain model structs. It had no tests,
no persistence, no platform layer, no explicit memory budget, no page cache, and no
archive abstraction. The working directory is not a Git worktree, so there was no
commit history or clean baseline to compare.

The original reader did preserve source dimensions and converted the decoded page to
RGB565. That was the correct image-quality direction. It decoded only one page at a
time, so it did not have the catastrophic “decode an entire chapter” behavior.

## Important findings and dispositions

| Area | Finding | Disposition |
|---|---|---|
| Image lifecycle | The visible page was freed before the replacement decoded. A corrupt next page left the reader blank and repeated navigation retried the same file forever. | Fixed with transactional loading and a per-chapter failed-page set. |
| Reader initialization | `App::init()` succeeded even when the RGB565 viewport or streaming texture failed. | Fixed; reader resources are validated and initialization reports the SDL error. |
| SDL_image setup | Initialization accepted a partial JPG/PNG codec result. | Fixed; both requested codecs must initialize. |
| Memory | `IMG_Load` plus RGB565 conversion had an unbounded JPEG peak and no accounting. | Fixed for JPEG with header-first budget checks and direct scanline-to-RGB565 decode. PNG retains the probed SDL_image fallback cost. |
| Ownership | `App` owned `MangaReader` through manual `new`/`delete`; SDL surfaces and textures were raw pointers. | Replaced with `std::unique_ptr` and SDL deleters. |
| Input | PSP analog input was collapsed to four digital thresholds, producing coarse pan movement. | Analog axes now retain magnitude and use dead-zone normalization; D-pad remains precise. |
| Frame behavior | Pan speed depended on a 45 ms polling gate rather than elapsed time. | Analog pan is frame-time based; D-pad repetition remains deliberately stepped. |
| Scanner | Hidden/system folders and explicit cover files were not handled; case-only ties were nondeterministic. | Added filtering, deterministic natural sorting, duplicate suppression, cover candidates, and fallback cover paths. |
| File errors | A bad image could prevent forward progress. | Navigation skips pages already proven unreadable and preserves the current image if a load fails. |
| Persistence | No settings, continue-reading, or progress existed. | Added versioned, atomic progress/settings files with corrupt-entry isolation. |
| PSP lifecycle | No HOME exit or power callback was registered. | Added PSP exit, suspend, and resume callbacks. Suspend and exit checkpoint progress; resume invalidates the viewport. |
| UI | The HUD was permanent and Start globally quit the app. | HUD now times out; global Start-to-quit was removed; Start continues the latest title from the library. |
| Testing | No non-hardware tests existed. | Added core, PageSource, PageCache, and direct JPEG synthetic tests plus a development benchmark. |

## Build and toolchain audit

The CMake use of `PSP` and `create_pbp_file()` is compatible with current PSPSDK:
the official toolchain file defines `PSP`, selects `psp-g++`, directs CMake package
lookups through `psp-pkg-config`, and includes `CreatePBP.cmake`. `MEMSIZE 1` is a
valid request for all memory available to the application. The project now emits the
SFO version as `01.00` and keeps the expected `EBOOT.PBP` packaging step.

Current PSPDEV packages provide `sdl2`, `sdl2-image`, and `sdl2-gfx`. They also
provide `minizip`, `libzip`, `unarr`, and `unrar`. The recommended CBZ dependency for
the next phase is `minizip`: it is purpose-built for bounded ZIP entry access and is
smaller in scope than a general archive framework. `unarr` is a plausible later CBR
backend, but it is LGPL-3.0 and does not support RAR5; those compatibility and
distribution implications must be accepted before integrating it.

The current machine has no installed SDL development libraries, `psp-cmake`,
`psp-g++`, or `PSPDEV` environment variable. A disposable portable CMake/Zig toolchain
built the core against pinned minizip-ng 3.0.4 and libjpeg-turbo 3.1.3. Debug and Release
CTest both passed 4/4 executables. The modified `ImageLoader`, `MangaReader`, and `App`
translation units also compiled with warnings enabled against official SDL headers.
These checks do not substitute for an SDL-linked application or PSP build.

## Memory observations

The resident reader representation is:

- one source-resolution RGB565 page: approximately `width * height * 2` bytes;
- one 480x272 RGB565 CPU viewport: about 255 KiB;
- one 480x272 RGB565 streaming texture: approximately 255 KiB, driver-dependent.

During a JPEG load, the only full-resolution new allocation is the final RGB565 page;
one scanline and libjpeg controller/MCU memory accompany it. A CBZ load additionally
retains the selected compressed entry, while a folder file is streamed. Transactional
loading also retains the visible page. Tracked bytes and estimated library overhead are
reported separately. PNG still holds an SDL_image surface while allocating its RGB565
replacement, but its IHDR-derived peak is checked before decode and measured again
before conversion.

Cooperative preload and the bounded N-1/N/N+1 cache are implemented together. They do
not increase the default budget or silently reduce source resolution. Tiled/region
decoding remains necessary when two transactional full-detail pages cannot coexist.

## Remaining PSP acceptance tests

These items require a PSPDEV build and preferably PSP-1000 plus PSP-2000/3000 or Go
hardware:

1. Build and inspect `EBOOT.PBP`, imports, size, and SFO memory flag.
2. Confirm SDL RGB565 streaming textures on the PSP renderer.
3. Exercise HOME exit, suspend during decode, resume, and repeated suspend cycles.
4. Verify `ms0:` and `ef0:` discovery, non-ASCII filenames, long paths, and FAT
   case behavior.
5. Profile direct JPG and fallback PNG decode peaks at 1000-2000 pixel widths on 32 MB and 64 MB
   models.
6. Confirm progress temp/backup replacement and recovery after forced power loss.
7. Measure analog dead zone, frame pacing, pan latency, and text readability on the
   physical 480x272 panel.
8. Run malformed/corrupt image fixtures and low-memory failure injection.

Nothing in this audit claims execution on real PSP hardware.
