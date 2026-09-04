# NexaManga PSP architecture

NexaManga PSP keeps filesystem, archive, decode, cache, and hardware concerns behind narrow
contracts so the memory-critical path can be tested without SDL or PSP hardware.

## Current systems

- `App`: main loop and explicit library, series, reader, bookmark, options, theme, and reader-overlay
  UI states. It performs event-driven checkpoints and does not decode or scan directly.
- `LibraryManager` / `FileScanner`: immutable library snapshot and lightweight folder/
  CBZ metadata. No page image is decoded during startup.
- `PageSource`: common count/name/ID/read/error contract. A folder read returns a path
  without copying the compressed file; a CBZ read extracts only the requested entry.
- `ImageDecoder`: representation-aware probe/decode boundary. `FullDetail`,
  `ScaledPreview`, and a reserved `Region` request keep future multi-resolution/tiled
  work out of `PageSource` and `PageCache`.
- `JpegDecoder`: safe libjpeg header probing plus grayscale/RGB scanline conversion
  directly into a caller-owned RGB565 target. It uses `jpeg_stdio_src()` for folder
  files and `jpeg_mem_src()` for CBZ bytes.
- `ImageProbe`: routes JPEG probing and performs a lightweight PNG signature/IHDR probe.
- `ImageLoader`: selects direct JPEG or probed SDL_image PNG fallback, checks the shared
  budget before full-size allocation where possible, and commits only complete surfaces.
- `PageCache`: SDL-independent deterministic N-1/N/N+1 cache. Decoded bytes and fixed
  viewport/texture estimates share one authoritative budget.
- `ReaderExperience`: SDL-independent direction, spread classification/source bounds,
  overlapped viewport stops, nearest-stop resynchronization, and completion rules.
- `MangaReader`: transactional physical-page/cache ownership plus viewport zoom/pan.
  It applies logical stops over the same decoded surface and retains cooperative preload.
- `ThumbnailCache`: app-only bounded LRU of 72x96 RGB565 textures. It reads explicit
  covers or fallback metadata through `PageSource`; JPEG uses native 1/8 or 1/4 scaling.
- `SaveManager`: version-2 progress, reader preferences, recency, bookmarks, and safe
  Continue/bookmark resolution. Version 1 migrates and malformed rows are isolated.
- `UiTheme` / `SettingsManager`: five fixed low-cost palettes and version-4 theme
  persistence. Palette changes affect UI primitives only; cover and reader textures
  are not recreated.
- `Input`, `Platform`, `SaveManager`, `SettingsManager`, `Persistence`, and `Log`: narrow
  platform, persistence, and development boundaries.

## Ownership and data flow

```text
folder JPEG path ---------------------> jpeg_stdio_src --+
                                                         |
CBZ archive -> selected entry bytes -> jpeg_mem_src -----+-> scanline -> RGB565 surface
                                                         |                    |
folder/CBZ PNG -> signature + IHDR probe -> SDL_image ----+                    v
                                                                    PageCache N-1/N/N+1
                                                                             |
                                                                             v
                                                               480x272 viewport + texture
```

CBZ central directories stay open in `CbzPageSource`, but only one selected compressed
entry vector exists during a load. It is released when `ImageLoader::loadRgb565()`
returns. Folder JPEGs never acquire an equivalent full-file vector. The final decoded
page is always source-resolution RGB565; the 480x272 surface is only a derived viewport.

Split halves never enter `PageCache`: they are source rectangles over one physical
decoded page. The physical page remains authoritative in persistence; logical position
is clamped when restored against changed geometry.

`MangaReader` does not replace its displayed `shared_ptr` until decode and cache
insertion both succeed. A corrupt, truncated, or over-budget replacement therefore
cannot invalidate the current page or create a half-populated cache entry. Required
budget failures get one retry after non-current neighbors are trimmed. The visible page
remains protected throughout that retry.

## Exact old and new JPEG peaks

Before the direct decoder, a folder JPEG cache miss retained existing cache pages,
the CPU viewport, an estimated streaming texture, a full SDL_image RGB/RGBA decode,
and a separately allocated RGB565 conversion target at the same time. A CBZ miss also
retained the selected compressed entry vector. The folder compressed file was streamed,
not copied. SDL_image/libjpeg allocator internals were not measurable by NexaManga PSP.

The direct baseline JPEG path retains:

```text
existing decoded cache pages
+ CPU viewport
+ estimated streaming texture
+ final source-resolution RGB565 target
+ one grayscale or RGB scanline
+ selected compressed CBZ entry (CBZ only)
+ estimated libjpeg controller/MCU state
+ estimated progressive coefficient storage when applicable
```

The overlay and result structures keep the first measurable group as `tracked` bytes
and the last decoder-owned group as `estimated library overhead`; the predicted peak is
their saturated sum. This is intentionally not presented as allocator-exact RSS.

For even-width examples, final RGB565 storage is:

| Source dimensions | Exact bytes | MiB |
|---|---:|---:|
| 1400x2100 | 5,880,000 | 5.6076 |
| 1600x2400 | 7,680,000 | 7.3242 |
| 2000x3000 | 12,000,000 | 11.4441 |
| 3000x4500 | 27,000,000 | 25.7492 |

Rows use four-byte-aligned pitch, so odd widths may add two padding bytes per row.
Feasibility is computed from the configured budget rather than a hardcoded dimension.
A page that fits alone may still fail transactional navigation when it cannot coexist
with the current page. It is rejected cleanly and never silently resized.

## Scaling and future region decoding

Full detail remains authoritative for reading and zoom. `ScaledPreview` accepts native
libjpeg DCT scale denominators 1, 2, 4, or 8, but it is an explicitly disposable derived
representation; the current reader does not automatically keep both full and reduced
copies. `Region` is represented in the request contract and currently returns a clear
unsupported error.

JPEG region work will not be free random access. MCU alignment, restart structure, and
the need to process or skip preceding scanlines can still make a crop proportional to
earlier image content. A future tiled reader must account for that work and must not
rewrite `PageSource`, cache ownership, or the full-detail contract.

## PNG and scheduling constraints

PNG retains SDL_image for this phase. Its IHDR is probed before decode and its predicted
peak includes selected CBZ bytes, a conservative full RGBA surface, the RGB565 target,
and estimated library workspace. The loader rechecks measured SDL surface bytes before
conversion. This path remains more transient-memory-heavy than direct JPEG.

Preload remains cooperative and synchronous: one N+1 or N-1 attempt per idle call. A
single decode is not time-sliced. Timing uses PSP kernel microseconds or desktop steady
clock and records source/archive read, header probe, JPEG decode, RGB565 output, and
total load. Release builds retain aggregate timing but development logging stays quiet.
