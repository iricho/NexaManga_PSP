# NexaManga PSP Roadmap

## v0.1 - First readable build
- [x] PSP SDL2 application shell
- [x] `/MANGA` scanner
- [x] natural sorting
- [x] series and chapter lists
- [x] JPG / PNG loading
- [x] source-resolution reader working surface
- [x] Fit Page / Fit Width
- [x] zoom
- [x] pan
- [x] L/R page navigation

## v0.2 - Make it pleasant
- [x] persistent progress per series/chapter/page
- [x] remember fit preference
- [x] cover detection (`cover.jpg`, first-page fallback metadata)
- [x] cover-grid library and bounded thumbnail previews
- [x] hide reader HUD after a short delay
- [x] bounded N-1/N/N+1 decoded-page cache
- [x] cooperative N+1 then N-1 idle preload
- [x] better analog panning
- [x] PSP suspend/resume callback foundation (hardware acceptance pending)

## v0.3 - Comic archives
- [x] CBZ support through a common page-source abstraction
- [x] archive chapter browsing without extracting everything
- [ ] CBR feasibility test
- [x] safe memory limits and corrupted archive/page handling

## v0.4 - PSP-specific image pipeline
- [x] direct grayscale/RGB JPEG scanline decode to source-resolution RGB565
- [x] avoid SDL_image double-allocation on JPEGs
- [x] JPEG/PNG header probing and pre-allocation budget rejection
- [x] folder `FILE*` and CBZ memory JPEG sources with safe libjpeg recovery
- [x] explicit full-detail vs 1/2, 1/4, and 1/8 preview representations
- [x] tracked-vs-estimated decode memory and timing diagnostics
- [ ] tiled/region decode for large pages
- [ ] optional 24/32-bit color mode for color comics
- [x] image decode/cache memory budget and instrumentation

## v0.5 - Manga reading features
- [x] RTL / LTR content ordering with stable R-forward/L-back controls
- [x] computed Smart Reading viewport stops with overlap and pan resync
- [x] Auto / Full / Split double-page spread handling
- [x] persistent bookmarks and bookmark navigation
- [x] reader-menu chapter list
- [x] physical page and chapter completion progress
- [x] first-class Continue Reading and recently-read ordering
- [x] dedicated cover/chapter series screen

## v0.6 - Smart panels
- [ ] panel metadata format
- [ ] manual panel regions
- [ ] PC-side optional panel detection
- [ ] panel-to-panel reading mode

## Later
- [ ] Windows companion/sync app
- [ ] Wi-Fi transfer
- [ ] metadata import
- [ ] reading statistics
