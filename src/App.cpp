#include "App.hpp"

#include "BootDiagnostics.hpp"
#include "BuildConfig.hpp"
#include "FileScanner.hpp"
#include "Log.hpp"
#include "NaturalSort.hpp"
#include "PathUtils.hpp"
#include "PerformanceClock.hpp"
#include "Platform.hpp"

#include <SDL2_gfxPrimitives.h>
#include <SDL_image.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
#include <sstream>
#include <utility>

namespace {

constexpr bool persistenceEnabled() {
    return !(MANGAPSP_DEVELOPMENT && MANGAPSP_IGNORE_PERSISTENCE);
}

void scanDiagnostic(const char* key, const char* value) {
#if MANGAPSP_PSP_RC
    // RC keeps scan failures and the final count, but omits candidate/page spam.
    if (!key || (std::strcmp(key, "scan_error") != 0 &&
                 std::strcmp(key, "scan_failure") != 0 &&
                 std::strcmp(key, "series_skipped") != 0 &&
                 std::strcmp(key, "chapter_skipped") != 0 &&
                 std::strcmp(key, "scan_series_count") != 0)) return;
#endif
    BootDiagnostics::note(key, value);
}

void drawText(SDL_Renderer* renderer, int x, int y, const std::string& text,
              Uint8 r = 235, Uint8 g = 235, Uint8 b = 235, Uint8 a = 255) {
    stringRGBA(
        renderer,
        static_cast<Sint16>(x),
        static_cast<Sint16>(y),
        const_cast<char*>(text.c_str()),
        r, g, b, a
    );
}

void setDrawColor(SDL_Renderer* renderer, const UiColor& color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

void drawText(SDL_Renderer* renderer, int x, int y, const std::string& text,
              const UiColor& color) {
    drawText(renderer, x, y, text, color.r, color.g, color.b, color.a);
}

void drawStrongText(SDL_Renderer* renderer, int x, int y, const std::string& text,
                    const UiColor& color) {
    drawText(renderer, x + 1, y, text, color);
    drawText(renderer, x, y, text, color);
}

std::string pageStatus(size_t current, size_t total) {
    std::ostringstream ss;
    ss << (current + 1) << " / " << total;
    return ss.str();
}

float chapterProgress(const ReadingProgress* progress, std::size_t pageCount) {
    if (!progress || pageCount == 0) return 0.0f;
    if (progress->completed) return 1.0f;
    return std::min(1.0f, static_cast<float>(progress->pageIndex + 1u) /
                           static_cast<float>(pageCount));
}

const char* sortModeName(std::size_t mode) {
    switch (mode % 4u) {
        case 0: return "Recent";
        case 1: return "Alphabetical";
        case 2: return "Progress";
        case 3: return "Favorites";
    }
    return "Recent";
}

} // namespace

App::App() = default;

App::~App() {
    saveCurrentProgress();
    reader_.reset();
    thumbnailCache_.reset();

    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }

    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    IMG_Quit();
    SDL_Quit();
}

bool App::init() {
    Log::info(std::string("Startup on ") + Platform::name());

    BootDiagnostics::note("build", MANGAPSP_PSP_RC ? "PSP RC" :
        (MANGAPSP_DEVELOPMENT ? "Development" : "Release"));
    BootDiagnostics::note("persistence",
        persistenceEnabled() ? "normal" : "ignored (development isolation)");
    BootDiagnostics::note("cbz",
        MANGAPSP_DEVELOPMENT && MANGAPSP_DISABLE_CBZ ? "disabled" : "enabled");
    BootDiagnostics::note("thumbnails",
        MANGAPSP_DEVELOPMENT && MANGAPSP_DISABLE_THUMBNAILS ? "disabled" : "enabled");
    BootDiagnostics::note("manga_scan",
        MANGAPSP_DEVELOPMENT && MANGAPSP_FORCE_NO_MANGA ? "forced empty" : "enabled");
    BootDiagnostics::note("psp_input_setup",
        MANGAPSP_DEVELOPMENT && MANGAPSP_DEFER_PSP_INPUT_SETUP
            ? "deferred until after SDL_Init" : "performed during App construction");

    if (persistenceEnabled() &&
        !settingsManager_.load() && !settingsManager_.lastError().empty()) {
        Log::warning(settingsManager_.lastError());
    }
#if MANGAPSP_HARDWARE_DIAGNOSTICS
    debugOverlay_ = settingsManager_.settings().debugOverlay;
#else
    debugOverlay_ = false;
#endif
    input_.setAnalogDeadZone(settingsManager_.settings().analogDeadZone);
    BootDiagnostics::stage(3, "settings ready / persistence decision applied");
    BootDiagnostics::holdIfRequested(3);

    BootDiagnostics::checkpoint(31, "stage-03 hold gate returned");
    BootDiagnostics::holdIfCheckpointRequested(31);

    BootDiagnostics::checkpoint(32, "before SDL flag preparation");
    const Uint32 sdlInitFlags = SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER;
    BootDiagnostics::checkpoint(33, "flags ready; immediately before SDL_Init");
    BootDiagnostics::noteUnsigned("sdl_init_flags",
        static_cast<unsigned long>(sdlInitFlags));
    BootDiagnostics::holdIfCheckpointRequested(33);

    const int sdlInitResult = SDL_Init(sdlInitFlags);
    BootDiagnostics::checkpoint(34, "SDL_Init returned");
    char sdlInitResultText[24];
    std::snprintf(sdlInitResultText, sizeof(sdlInitResultText), "%d", sdlInitResult);
    BootDiagnostics::note("sdl_init_result", sdlInitResultText);
    BootDiagnostics::holdIfCheckpointRequested(34);

    if (sdlInitResult != 0) {
        BootDiagnostics::checkpoint(35, "failure branch; before SDL_GetError");
        const char* sdlError = SDL_GetError();
        BootDiagnostics::checkpoint(36, "SDL_GetError returned");
        BootDiagnostics::note("sdl_init_error", sdlError ? sdlError : "unknown");
        lastError_ = std::string("SDL initialization failed: ") +
            (sdlError ? sdlError : "unknown");
        return false;
    }
    BootDiagnostics::checkpoint(37, "success branch; immediately before stage 04");
    BootDiagnostics::holdIfCheckpointRequested(37);
    BootDiagnostics::stage(4, "SDL initialized");
    BootDiagnostics::note("sdl_video_driver",
        SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "unknown");
    BootDiagnostics::holdIfRequested(4);

#if MANGAPSP_DEVELOPMENT && MANGAPSP_DEFER_PSP_INPUT_SETUP
    BootDiagnostics::note("psp_input_setup", "deferred until after SDL_Init");
    input_.initializePlatform();
#endif

    const int imageFlags = IMG_INIT_JPG | IMG_INIT_PNG;
    if ((IMG_Init(imageFlags) & imageFlags) != imageFlags) {
        lastError_ = std::string("SDL_image initialization failed: ") + IMG_GetError();
        return false;
    }
    BootDiagnostics::stage(5, "SDL_image JPG+PNG initialized");
    BootDiagnostics::holdIfRequested(5);

    window_ = SDL_CreateWindow(
        "NexaManga PSP",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        ScreenWidth,
        ScreenHeight,
        0
    );

    if (!window_) {
        lastError_ = std::string("Window creation failed: ") + SDL_GetError();
        return false;
    }
    BootDiagnostics::stage(6, "window created");
    BootDiagnostics::holdIfRequested(6);

    renderer_ = SDL_CreateRenderer(
        window_,
        -1,
        SDL_RENDERER_ACCELERATED
    );

    if (!renderer_) {
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    }

    if (!renderer_) {
        lastError_ = std::string("Renderer creation failed: ") + SDL_GetError();
        return false;
    }

    SDL_RendererInfo rendererInfo {};
    if (SDL_GetRendererInfo(renderer_, &rendererInfo) == 0) {
        BootDiagnostics::note("sdl_renderer", rendererInfo.name ? rendererInfo.name : "unknown");
        BootDiagnostics::noteUnsigned("sdl_renderer_flags",
            static_cast<unsigned long>(rendererInfo.flags));
        bool supportsRgb565 = false;
        for (Uint32 i = 0; i < rendererInfo.num_texture_formats; ++i) {
            if (rendererInfo.texture_formats[i] == SDL_PIXELFORMAT_RGB565) {
                supportsRgb565 = true;
                break;
            }
        }
        BootDiagnostics::note("sdl_renderer_rgb565",
            supportsRgb565 ? "advertised" : "not advertised");
    } else {
        BootDiagnostics::note("sdl_renderer_info_error", SDL_GetError());
    }
    int outputWidth = 0;
    int outputHeight = 0;
    if (SDL_GetRendererOutputSize(renderer_, &outputWidth, &outputHeight) == 0) {
        BootDiagnostics::noteUnsigned("sdl_output_width",
            static_cast<unsigned long>(outputWidth));
        BootDiagnostics::noteUnsigned("sdl_output_height",
            static_cast<unsigned long>(outputHeight));
    } else {
        BootDiagnostics::note("sdl_output_size_error", SDL_GetError());
    }
    BootDiagnostics::stage(7, "renderer created");
    BootDiagnostics::holdIfRequested(7, renderer_);
    BootDiagnostics::checkpointLogOnly("07.1", "stage-07 hold gate returned");

    BootDiagnostics::checkpointLogOnly("07.2", "immediately before SDL_SetRenderDrawBlendMode");
    const int blendModeResult = SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    BootDiagnostics::checkpointLogOnly("07.3", "SDL_SetRenderDrawBlendMode returned");
    char blendModeResultText[24];
    std::snprintf(blendModeResultText, sizeof(blendModeResultText), "%d", blendModeResult);
    BootDiagnostics::note("sdl_blend_result", blendModeResultText);
    if (blendModeResult != 0) {
        BootDiagnostics::checkpointLogOnly("07.4", "blend failure branch; before SDL_GetError");
        const char* blendError = SDL_GetError();
        BootDiagnostics::checkpointLogOnly("07.5", "SDL_GetError returned");
        Log::warning(std::string("Renderer blend mode unavailable: ") +
            (blendError ? blendError : "unknown"));
        BootDiagnostics::note("sdl_blend_error", blendError ? blendError : "unknown");
        BootDiagnostics::checkpointLogOnly("07.6", "blend failure diagnostics completed");
    }

    BootDiagnostics::checkpointLogOnly("07.7", "before settings image-budget read");
    const std::size_t requestedImageBudget =
        settingsManager_.settings().imageMemoryBudgetBytes;
    BootDiagnostics::noteUnsigned("requested_image_budget_bytes",
        static_cast<unsigned long>(requestedImageBudget));
    BootDiagnostics::checkpointLogOnly("07.8", "before budget clamp using early memory profile");
    const std::size_t imageBudget = Platform::clampImageMemoryBudget(requestedImageBudget);
    BootDiagnostics::checkpointLogOnly("07.9", "budget clamp returned");
    BootDiagnostics::noteUnsigned("image_budget_bytes", static_cast<unsigned long>(imageBudget));
    BootDiagnostics::checkpointLogOnly("07.10", "before hardwareInfo snapshot");
    const Platform::HardwareInfo hardware = Platform::hardwareInfo();
    BootDiagnostics::checkpointLogOnly("07.11", "hardwareInfo returned");
    BootDiagnostics::note("psp_model", hardware.model);
    BootDiagnostics::note("memory_profile", hardware.memoryProfile);
    BootDiagnostics::noteUnsigned("startup_max_free_block_bytes",
        static_cast<unsigned long>(hardware.freeUserMemoryBytes));
    BootDiagnostics::note("free_user_memory_measured",
        hardware.freeMemoryMeasured ? "yes" : "no; conservative fallback");
    BootDiagnostics::checkpointLogOnly("07.12", "memory notes complete; immediately before stage 08");
    BootDiagnostics::stage(8, "memory profile selected");
    BootDiagnostics::holdIfRequested(8, renderer_);

#if defined(__PSP__) && MANGAPSP_DEVELOPMENT && MANGAPSP_BOOT_DIAGNOSTICS && \
    MANGAPSP_HOLD_AFTER_FOLDER_SCAN
    // This diagnostic-only branch scans and holds before MangaReader or
    // ThumbnailCache construction, so no page decode or cover path can run.
    BootDiagnostics::checkpointLogOnly("08.1", "before folder root resolution (hold build)");
    libraryManager_.setRoot(FileScanner::findMangaRoot(scanDiagnostic));
    BootDiagnostics::note("root", libraryManager_.root().c_str());
    BootDiagnostics::checkpointLogOnly("08.2", "before folder library scan (hold build)");
    libraryManager_.refresh(scanDiagnostic);
    BootDiagnostics::checkpointLogOnly("08.3", "folder library scan returned (hold build)");
    BootDiagnostics::noteUnsigned("series_count",
        static_cast<unsigned long>(libraryManager_.series().size()));
    BootDiagnostics::note("folder_scan_hold", "entered before reader and thumbnails");

    SDL_SetRenderDrawColor(renderer_, 12, 20, 28, 255);
    SDL_RenderClear(renderer_);
    drawText(renderer_, 42, 82, "Folder scan complete", 245, 245, 245, 255);
    drawText(renderer_, 42, 110,
             "Series count: " + std::to_string(libraryManager_.series().size()),
             220, 230, 235, 255);
    drawText(renderer_, 42, 138, "Root: " + libraryManager_.root(),
             220, 230, 235, 255);
    SDL_RenderPresent(renderer_);
    while (true) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {}
        SDL_Delay(100);
    }
#endif

    reader_.reset(new (std::nothrow) MangaReader(
        renderer_, ScreenWidth, ScreenHeight, imageBudget));
    if (!reader_) {
        lastError_ = "Reader allocation failed.";
        return false;
    }
    if (!reader_->isReady()) {
        lastError_ = reader_->lastError();
        return false;
    }
    BootDiagnostics::stage(9, "reader created and ready");
    BootDiagnostics::holdIfRequested(9, renderer_);

#if MANGAPSP_DEVELOPMENT && MANGAPSP_DISABLE_THUMBNAILS
    thumbnailCache_.reset();
#else
    thumbnailCache_.reset(new (std::nothrow) ThumbnailCache(renderer_));
#endif
    BootDiagnostics::stage(10, "thumbnail decision applied");
    BootDiagnostics::holdIfRequested(10, renderer_);

    if (!Platform::initializeLifecycle()) {
        Log::warning("PSP lifecycle callback initialization failed.");
        BootDiagnostics::note("lifecycle_callbacks", "initialization failed; continuing");
    } else {
        BootDiagnostics::note("lifecycle_callbacks", "ready");
    }
    BootDiagnostics::stage(11, "lifecycle callbacks attempted");
    BootDiagnostics::holdIfRequested(11, renderer_);

#if MANGAPSP_DEVELOPMENT && MANGAPSP_FORCE_NO_MANGA
    libraryManager_.setRoot("<diagnostic no-manga>");
#else
    BootDiagnostics::checkpointLogOnly("11.1", "before manga root resolution");
    libraryManager_.setRoot(FileScanner::findMangaRoot(scanDiagnostic));
    BootDiagnostics::checkpointLogOnly("11.2", "manga root resolution returned");
#endif
    BootDiagnostics::note("root", libraryManager_.root().c_str());
    BootDiagnostics::note("manga_root", libraryManager_.root().c_str());
    BootDiagnostics::stage(12, "manga root selected");
    BootDiagnostics::holdIfRequested(12, renderer_);

    BootDiagnostics::checkpointLogOnly("12.1", "before folder library scan");
    refreshLibrary();
    BootDiagnostics::checkpointLogOnly("12.2", "folder library scan returned");
    if (persistenceEnabled() &&
        !saveManager_.load() && !saveManager_.lastError().empty()) {
        Log::warning(saveManager_.lastError());
    }
    rebuildLibraryOrder();
    const std::string& savedSelection = settingsManager_.settings().lastSelectedSeriesPath;
    for (std::size_t i = 0; i < libraryManager_.series().size(); ++i) {
        if (PathUtils::normalizedKey(libraryManager_.series()[i].path) ==
            PathUtils::normalizedKey(savedSelection)) {
            libraryState_.selectedSeries = i;
            break;
        }
    }
    BootDiagnostics::noteUnsigned("series_count",
        static_cast<unsigned long>(libraryManager_.series().size()));
    BootDiagnostics::stage(13, "library scan and progress decision complete");
    BootDiagnostics::holdIfRequested(13, renderer_);
    lastFrameTick_ = SDL_GetTicks();

    return true;
}

void App::refreshLibrary() {
#if MANGAPSP_DEVELOPMENT && MANGAPSP_FORCE_NO_MANGA
    lastLibraryOpenMicros_ = 0;
    libraryOrder_.clear();
    if (thumbnailCache_) thumbnailCache_->clear();
    return;
#endif
    const std::uint64_t started = PerformanceClock::nowMicros();
    libraryManager_.refresh(scanDiagnostic);
    lastLibraryOpenMicros_ = PerformanceClock::elapsedMicros(started);
    const auto& library = libraryManager_.series();

    std::ostringstream message;
    message << "Library scan: " << library.size() << " series under " << libraryManager_.root();
    Log::info(message.str());

    if (libraryState_.selectedSeries >= library.size()) {
        libraryState_.selectedSeries = library.empty() ? 0 : library.size() - 1;
    }
    if (thumbnailCache_) thumbnailCache_->clear();
    rebuildLibraryOrder();
}

void App::rebuildLibraryOrder() {
    const auto& library = libraryManager_.series();
    libraryOrder_.clear();
    if (libraryState_.sortMode % 4u == 0u) {
        libraryOrder_ = saveManager_.recentlyReadSeries(library);
        for (std::size_t i = 0; i < library.size(); ++i) {
            if (std::find(libraryOrder_.begin(), libraryOrder_.end(), i) == libraryOrder_.end()) {
                libraryOrder_.push_back(i);
            }
        }
        return;
    }

    for (std::size_t i = 0; i < library.size(); ++i) libraryOrder_.push_back(i);
    auto progressFor = [&](std::size_t index) {
        float total = 0.0f;
        for (const Chapter& chapter : library[index].chapters) {
            total += chapterProgress(saveManager_.find(library[index], chapter), chapter.pageCount());
        }
        return library[index].chapters.empty() ? 0.0f :
            total / static_cast<float>(library[index].chapters.size());
    };
    auto favoriteFor = [&](std::size_t index) {
        for (const Chapter& chapter : library[index].chapters) {
            const ReadingProgress* progress = saveManager_.find(library[index], chapter);
            if (progress && progress->favorite) return true;
        }
        return false;
    };
    std::stable_sort(libraryOrder_.begin(), libraryOrder_.end(), [&](std::size_t left, std::size_t right) {
        if (libraryState_.sortMode % 4u == 2u) {
            const float l = progressFor(left);
            const float r = progressFor(right);
            if (l != r) return l > r;
        } else if (libraryState_.sortMode % 4u == 3u) {
            const bool l = favoriteFor(left);
            const bool r = favoriteFor(right);
            if (l != r) return l;
        }
        return NaturalSort::less(library[left].name, library[right].name);
    });
}

bool App::openSelectedChapter(bool resume, std::size_t requestedPage) {
    const auto& library = libraryManager_.series();
    if (!reader_ || libraryState_.selectedSeries >= library.size()) return false;
    const Series& series = library[libraryState_.selectedSeries];
    if (seriesState_.selectedChapter >= series.chapters.size()) return false;
    const Chapter& chapter = series.chapters[seriesState_.selectedChapter];

    const ReadingProgress* progress = resume ? saveManager_.find(series, chapter) : nullptr;
    const SavedFitMode savedFit = progress ? progress->fitMode
                                           : settingsManager_.settings().defaultFitMode;
    reader_->setFitMode(savedFit == SavedFitMode::Width
        ? MangaReader::FitMode::Width : MangaReader::FitMode::Page);
    reader_->setReadingDirection(progress && progress->directionOverride
        ? progress->direction : settingsManager_.settings().defaultDirection);
    reader_->setSmartReading(progress ? progress->smartReading
                                      : settingsManager_.settings().smartReading);
    reader_->setSpreadMode(progress ? progress->spreadMode
                                    : settingsManager_.settings().defaultSpreadMode);

    const std::size_t startPage = requestedPage != std::numeric_limits<std::size_t>::max()
        ? requestedPage : progress ? progress->pageIndex : 0;
    const std::uint64_t started = PerformanceClock::nowMicros();
    const bool opened = reader_->openChapter(&chapter, startPage);
    lastChapterOpenMicros_ = PerformanceClock::elapsedMicros(started);
    if (!opened) {
        lastError_ = reader_->lastError();
        return false;
    }
    if (progress) reader_->restoreLogicalPosition(progress->logicalPosition);

    lastError_.clear();
    screen_ = Screen::Reader;
    readerOverlay_.menuOpen = false;
    lastReaderInputTick_ = SDL_GetTicks();
    readerHudUntil_ = lastReaderInputTick_ + settingsManager_.settings().hudTimeoutMs;
    saveCurrentProgress();
    return true;
}

bool App::continueMostRecent() {
    const ReadingProgress* recent = saveManager_.mostRecent();
    if (!recent) return false;

    ProgressLocation location;
    if (!saveManager_.resolve(libraryManager_.series(), *recent, location)) {
        lastError_ = "The most recently read series is no longer available.";
        return false;
    }
    libraryState_.selectedSeries = location.seriesIndex;
    seriesState_.selectedChapter = location.chapterIndex;
    const bool opened = openSelectedChapter(true, location.pageIndex);
    if (opened) reader_->restoreLogicalPosition(location.logicalPosition);
    if (opened && location.recoveredChapter) {
        Log::warning("Continue Reading recovered to an available chapter in the same series.");
    }
    return opened;
}

void App::saveCurrentProgress() {
    if (!persistenceEnabled()) return;
    if (!reader_ || screen_ != Screen::Reader || reader_->pageCount() == 0) return;
    const auto& library = libraryManager_.series();
    if (libraryState_.selectedSeries >= library.size()) return;
    const Series& series = library[libraryState_.selectedSeries];
    if (seriesState_.selectedChapter >= series.chapters.size()) return;
    const Chapter& chapter = series.chapters[seriesState_.selectedChapter];

    ReadingProgress progress;
    if (const ReadingProgress* existing = saveManager_.find(series, chapter)) progress = *existing;
    progress.seriesPath = series.path;
    progress.seriesName = series.name;
    progress.chapterPath = chapter.path;
    progress.chapterName = chapter.name;
    progress.pageIndex = reader_->currentPageIndex();
    progress.fitMode = reader_->fitMode() == MangaReader::FitMode::Width
        ? SavedFitMode::Width : SavedFitMode::Page;
    progress.direction = reader_->readingDirection();
    progress.directionOverride = progress.direction != settingsManager_.settings().defaultDirection;
    progress.smartReading = reader_->smartReading();
    progress.spreadMode = reader_->spreadMode();
    progress.logicalPosition = reader_->logicalPosition();
    progress.completed = progress.completed || reader_->completionReached();
    progress.lastReadOrder = saveManager_.nextReadOrder();
    saveManager_.update(std::move(progress));
    if (!saveManager_.save()) Log::error(saveManager_.lastError());
    rebuildLibraryOrder();
}

void App::persistSettings() {
    if (persistenceEnabled() && !settingsManager_.save()) Log::error(settingsManager_.lastError());
}

void App::openOptions(Screen returnScreen) {
    optionsReturnScreen_ = returnScreen;
    optionsState_.selection = 0;
    screen_ = Screen::Options;
}

void App::openThemePicker(Screen returnScreen) {
    returnScreen_ = returnScreen;
    themeState_.selection = static_cast<std::size_t>(settingsManager_.settings().selectedTheme);
    screen_ = Screen::Theme;
}

int App::run() {
    while (running_) {
        handleLifecycle();
        if (!running_) break;
        if (suspended_) {
            SDL_Delay(16);
            continue;
        }

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running_ = false;
            }
        }

        const Uint32 frameTick = SDL_GetTicks();
        frameSeconds_ = std::min(0.1f, static_cast<float>(frameTick - lastFrameTick_) / 1000.0f);
        lastFrameTick_ = frameTick;

        const InputFrame frame = input_.poll();
        handleInput(frame);
        if (screen_ == Screen::Reader && frame.held == 0 && frame.pressed == 0 &&
            frame.analogX == 0.0f && frame.analogY == 0.0f &&
            frameTick - lastReaderInputTick_ >= 100) {
            reader_->preloadOne();
        }

        render();
        if (!loopAliveReported_) {
            loopAliveReported_ = true;
            BootDiagnostics::stage(16, "run loop alive");
            BootDiagnostics::holdIfRequested(16, renderer_);
        }

        SDL_Delay(16);
    }

    saveCurrentProgress();
    return 0;
}

void App::handleLifecycle() {
    const Platform::LifecycleEvents events = Platform::pollLifecycleEvents();
    if (events.suspending) {
        Log::info("PSP suspend requested.");
        saveCurrentProgress();
        if (reader_) reader_->prepareForSuspend();
        suspended_ = true;
        ++suspendCount_;
    }
    if (events.resumed) {
        Log::info("PSP resume completed.");
        if (reader_) reader_->resumeAfterSuspend();
        suspended_ = false;
        ++resumeCount_;
    }
    if (events.exitRequested) {
        saveCurrentProgress();
        running_ = false;
    }
}

void App::handleInput(const InputFrame& input) {
#if MANGAPSP_HARDWARE_DIAGNOSTICS
    if (input.isPressed(Button::Debug)) {
        debugOverlay_ = !debugOverlay_;
        settingsManager_.settings().debugOverlay = debugOverlay_;
        if (persistenceEnabled() && !settingsManager_.save()) {
            Log::error(settingsManager_.lastError());
        }
    }
#endif

    switch (screen_) {
        case Screen::Library:
            handleLibraryInput(input);
            break;
        case Screen::Series:
            handleChapterInput(input);
            break;
        case Screen::Reader:
            handleReaderInput(input);
            break;
        case Screen::Bookmarks:
            handleBookmarkInput(input);
            break;
        case Screen::Options:
            handleOptionsInput(input);
            break;
        case Screen::Theme:
            handleThemeInput(input);
            break;
        case Screen::About:
            handleAboutInput(input);
            break;
    }
}

void App::handleLibraryInput(const InputFrame& input) {
    const auto& library = libraryManager_.series();

    if (library.empty() && input.isPressed(Button::Confirm)) {
        refreshLibrary();
        return;
    }

    auto selectedOrder = [&]() {
        const auto found = std::find(libraryOrder_.begin(), libraryOrder_.end(),
                                     libraryState_.selectedSeries);
        return found == libraryOrder_.end() ? std::size_t {0} :
               static_cast<std::size_t>(found - libraryOrder_.begin());
    };
    if (input.isPressed(Button::Up) && saveManager_.mostRecent()) {
        libraryState_.continueFocused = true;
    }
    if (input.isPressed(Button::Down)) libraryState_.continueFocused = false;
    if (!libraryState_.continueFocused && !libraryOrder_.empty()) {
        std::size_t order = selectedOrder();
        if (input.isPressed(Button::Left)) order = order == 0 ? libraryOrder_.size() - 1 : order - 1;
        if (input.isPressed(Button::Right)) order = (order + 1) % libraryOrder_.size();
        if (input.isPressed(Button::PrevPage)) order = order >= 4 ? order - 4 : 0;
        if (input.isPressed(Button::NextPage)) order = std::min(order + 4, libraryOrder_.size() - 1);
        libraryState_.selectedSeries = libraryOrder_[order];
    }

    if (input.isPressed(Button::Confirm) && libraryState_.continueFocused && saveManager_.mostRecent()) {
        continueMostRecent();
    } else if (input.isPressed(Button::Confirm) && !library.empty()) {
        seriesState_.selectedChapter = 0;

        const Series& series = library[libraryState_.selectedSeries];

        if (series.chapters.size() == 1 && series.chapters[0].name == "Pages") {
            openSelectedChapter(true);
        } else {
            std::uint64_t newestOrder = 0;
            for (std::size_t index = 0; index < series.chapters.size(); ++index) {
                const ReadingProgress* progress = saveManager_.find(series, series.chapters[index]);
                if (progress && progress->lastReadOrder >= newestOrder) {
                    newestOrder = progress->lastReadOrder;
                    seriesState_.selectedChapter = index;
                }
            }
            screen_ = Screen::Series;
        }
        settingsManager_.settings().lastSelectedSeriesPath = series.path;
        if (persistenceEnabled()) settingsManager_.save();
    }

    if (input.isPressed(Button::Start)) continueMostRecent();
    if (input.isPressed(Button::ToggleMode)) {
        libraryState_.sortMode = (libraryState_.sortMode + 1u) % 4u;
        rebuildLibraryOrder();
    }
    if (input.isPressed(Button::ZoomOut)) {
        openOptions(Screen::Library);
    }

    if (input.isPressed(Button::Back)) {
        running_ = false;
    }
}

void App::handleChapterInput(const InputFrame& input) {
    const auto& library = libraryManager_.series();
    if (library.empty() || libraryState_.selectedSeries >= library.size()) {
        screen_ = Screen::Library;
        return;
    }

    const Series& series = library[libraryState_.selectedSeries];
    const size_t count = series.chapters.size();

    if (input.isPressed(Button::Up) && count > 0) {
        seriesState_.selectedChapter = seriesState_.selectedChapter == 0 ? count - 1 : seriesState_.selectedChapter - 1;
    }

    if (input.isPressed(Button::Down) && count > 0) {
        seriesState_.selectedChapter = (seriesState_.selectedChapter + 1) % count;
    }

    if (input.isPressed(Button::Confirm) && count > 0) {
        openSelectedChapter(true);
    }

    if (input.isPressed(Button::Back)) {
        screen_ = Screen::Library;
    }
    if (input.isPressed(Button::Start)) continueMostRecent();
    if (input.isPressed(Button::ZoomOut)) {
        openOptions(Screen::Series);
    }
}

void App::handleReaderInput(const InputFrame& input) {
#if MANGAPSP_HARDWARE_DIAGNOSTICS
    if (hardwareTestOverlay_) {
        if (input.isPressed(Button::Back) || input.isPressed(Button::Select) ||
            input.isPressed(Button::Confirm)) hardwareTestOverlay_ = false;
        return;
    }
#endif
    const auto& library = libraryManager_.series();
    if (input.pressed != 0 || input.analogX != 0.0f || input.analogY != 0.0f) {
        lastReaderInputTick_ = SDL_GetTicks();
        readerHudUntil_ = lastReaderInputTick_ + settingsManager_.settings().hudTimeoutMs;
    }
    if (input.isPressed(Button::Select)) {
        readerOverlay_.menuOpen = !readerOverlay_.menuOpen;
        return;
    }
    if (readerOverlay_.menuOpen) {
#if MANGAPSP_HARDWARE_DIAGNOSTICS
        constexpr std::size_t itemCount = 10;
#else
        constexpr std::size_t itemCount = 8;
#endif
        if (input.isPressed(Button::Up)) {
            readerOverlay_.menuSelection = readerOverlay_.menuSelection == 0
                ? itemCount - 1 : readerOverlay_.menuSelection - 1;
        }
        if (input.isPressed(Button::Down)) {
            readerOverlay_.menuSelection = (readerOverlay_.menuSelection + 1) % itemCount;
        }
        if (input.isPressed(Button::Confirm)) activateReaderMenuItem();
        if (input.isPressed(Button::Back)) readerOverlay_.menuOpen = false;
        return;
    }
    if (input.isPressed(Button::Back)) {
        saveCurrentProgress();
        reader_->close();

        if (!library.empty() &&
            libraryState_.selectedSeries < library.size() &&
            library[libraryState_.selectedSeries].chapters.size() == 1 &&
            library[libraryState_.selectedSeries].chapters[0].name == "Pages") {
            screen_ = Screen::Library;
        } else {
            screen_ = Screen::Series;
        }

        return;
    }

    if (input.isPressed(Button::PrevPage)) {
        const std::uint64_t started = PerformanceClock::nowMicros();
        if (reader_->retreatReading()) saveCurrentProgress();
        lastPageSwitchMicros_ = PerformanceClock::elapsedMicros(started);
    }

    if (input.isPressed(Button::NextPage)) {
        const std::uint64_t started = PerformanceClock::nowMicros();
        if (reader_->advanceReading()) saveCurrentProgress();
        lastPageSwitchMicros_ = PerformanceClock::elapsedMicros(started);
    }

    if (input.isPressed(Button::Confirm)) {
        reader_->smartZoomIn();
    }

    if (input.isPressed(Button::ZoomOut)) {
        reader_->zoomOut();
    }

    if (input.isPressed(Button::ToggleMode)) {
        reader_->toggleFitMode();
        saveCurrentProgress();
    }

    const float analogPixelsPerSecond = 190.0f * settingsManager_.settings().analogSensitivity;
    if (input.analogX != 0.0f || input.analogY != 0.0f) {
        reader_->pan(input.analogX * analogPixelsPerSecond * frameSeconds_,
                     input.analogY * analogPixelsPerSecond * frameSeconds_);
    }

    const Uint32 now = SDL_GetTicks();
    const bool canPan = (now - lastPanTick_) >= 45;

    if (canPan) {
        constexpr float step = 24.0f;
        bool moved = false;

        if (input.isHeld(Button::Left)) {
            reader_->pan(-step, 0.0f);
            moved = true;
        }
        if (input.isHeld(Button::Right)) {
            reader_->pan(step, 0.0f);
            moved = true;
        }
        if (input.isHeld(Button::Up)) {
            reader_->pan(0.0f, -step);
            moved = true;
        }
        if (input.isHeld(Button::Down)) {
            reader_->pan(0.0f, step);
            moved = true;
        }

        if (moved) {
            lastPanTick_ = now;
        }
    }
}

void App::activateReaderMenuItem() {
    switch (readerOverlay_.menuSelection) {
        case 0:
            reader_->setReadingDirection(reader_->readingDirection() == ReadingDirection::RightToLeft
                ? ReadingDirection::LeftToRight : ReadingDirection::RightToLeft);
            break;
        case 1:
            reader_->setSmartReading(!reader_->smartReading());
            break;
        case 2: {
            const SpreadMode next = reader_->spreadMode() == SpreadMode::Auto
                ? SpreadMode::FullSpread : reader_->spreadMode() == SpreadMode::FullSpread
                    ? SpreadMode::SplitSpread : SpreadMode::Auto;
            reader_->setSpreadMode(next);
            break;
        }
        case 3:
            reader_->toggleFitMode();
            break;
        case 4:
            saveCurrentProgress();
            openThemePicker(Screen::Reader);
            break;
        case 5:
            addCurrentBookmark();
            break;
        case 6:
            bookmarkState_.openedFromReader = true;
            bookmarkState_.selection = 0;
            screen_ = Screen::Bookmarks;
            break;
        case 7:
            saveCurrentProgress();
            reader_->close();
            screen_ = Screen::Series;
            break;
        case 8:
#if MANGAPSP_HARDWARE_DIAGNOSTICS
            debugOverlay_ = !debugOverlay_;
            settingsManager_.settings().debugOverlay = debugOverlay_;
            if (persistenceEnabled()) settingsManager_.save();
#endif
            break;
        case 9:
#if MANGAPSP_HARDWARE_DIAGNOSTICS
            hardwareTestOverlay_ = true;
#endif
            break;
    }
    if (screen_ == Screen::Reader) {
        saveCurrentProgress();
        readerOverlay_.menuOpen = false;
    }
}

void App::addCurrentBookmark() {
    const auto& library = libraryManager_.series();
    if (!reader_ || libraryState_.selectedSeries >= library.size()) return;
    const Series& series = library[libraryState_.selectedSeries];
    if (seriesState_.selectedChapter >= series.chapters.size()) return;
    const Chapter& chapter = series.chapters[seriesState_.selectedChapter];
    Bookmark bookmark;
    bookmark.seriesPath = series.path;
    bookmark.seriesName = series.name;
    bookmark.chapterPath = chapter.path;
    bookmark.chapterName = chapter.name;
    bookmark.pageIndex = reader_->currentPageIndex();
    bookmark.logicalPosition = reader_->logicalPosition();
    bookmark.label = chapter.name + " p." + std::to_string(bookmark.pageIndex + 1u);
    bookmark.creationOrder = saveManager_.nextBookmarkOrder();
    saveManager_.addBookmark(std::move(bookmark));
    if (persistenceEnabled() && !saveManager_.save()) Log::error(saveManager_.lastError());
}

bool App::openBookmark(std::size_t index) {
    if (index >= saveManager_.bookmarks().size()) return false;
    ProgressLocation location;
    if (!saveManager_.resolve(libraryManager_.series(), saveManager_.bookmarks()[index], location)) {
        lastError_ = "Bookmark source is no longer available.";
        return false;
    }
    libraryState_.selectedSeries = location.seriesIndex;
    seriesState_.selectedChapter = location.chapterIndex;
    const bool opened = openSelectedChapter(true, location.pageIndex);
    if (opened) reader_->restoreLogicalPosition(location.logicalPosition);
    return opened;
}

void App::handleBookmarkInput(const InputFrame& input) {
    const std::size_t count = saveManager_.bookmarks().size();
    if (input.isPressed(Button::Up) && count) {
        bookmarkState_.selection = bookmarkState_.selection == 0 ? count - 1 : bookmarkState_.selection - 1;
    }
    if (input.isPressed(Button::Down) && count) {
        bookmarkState_.selection = (bookmarkState_.selection + 1) % count;
    }
    if (input.isPressed(Button::Confirm) && count) openBookmark(bookmarkState_.selection);
    if (input.isPressed(Button::Back)) {
        if (bookmarkState_.openedFromReader && reader_ && reader_->pageCount() > 0) {
            screen_ = Screen::Reader;
            readerOverlay_.menuOpen = true;
        } else screen_ = Screen::Library;
    }
}

void App::handleOptionsInput(const InputFrame& input) {
#if MANGAPSP_HARDWARE_DIAGNOSTICS
    constexpr std::size_t itemCount = 5;
#else
    constexpr std::size_t itemCount = 4;
#endif
    if (input.isPressed(Button::Up)) {
        optionsState_.selection = optionsState_.selection == 0 ? itemCount - 1u : optionsState_.selection - 1u;
    }
    if (input.isPressed(Button::Down)) optionsState_.selection = (optionsState_.selection + 1u) % itemCount;
    if (input.isPressed(Button::Back)) {
        screen_ = optionsReturnScreen_;
        return;
    }
    if (!input.isPressed(Button::Confirm)) return;

    switch (optionsState_.selection) {
        case 0:
            openThemePicker(Screen::Options);
            break;
        case 1: {
            unsigned int& timeout = settingsManager_.settings().hudTimeoutMs;
            timeout = timeout < 2000u ? 2500u : timeout < 3500u ? 4000u : 1500u;
            persistSettings();
            break;
        }
        case 2:
            settingsManager_.settings().defaultDirection =
                settingsManager_.settings().defaultDirection == ReadingDirection::RightToLeft
                    ? ReadingDirection::LeftToRight : ReadingDirection::RightToLeft;
            persistSettings();
            break;
        case 3:
            screen_ = Screen::About;
            break;
        case 4:
#if MANGAPSP_HARDWARE_DIAGNOSTICS
            debugOverlay_ = !debugOverlay_;
            settingsManager_.settings().debugOverlay = debugOverlay_;
            persistSettings();
#endif
            break;
    }
}

void App::handleThemeInput(const InputFrame& input) {
    const std::size_t count = uiThemeCount();
    bool changed = false;
    if (input.isPressed(Button::Up)) {
        themeState_.selection = themeState_.selection == 0 ? count - 1u : themeState_.selection - 1u;
        changed = true;
    }
    if (input.isPressed(Button::Down)) {
        themeState_.selection = (themeState_.selection + 1u) % count;
        changed = true;
    }
    if (changed) {
        settingsManager_.settings().selectedTheme = uiThemeIdAt(themeState_.selection);
        persistSettings();
    }
    if (input.isPressed(Button::Confirm) || input.isPressed(Button::Back)) {
        screen_ = returnScreen_;
        if (screen_ == Screen::Reader) readerOverlay_.menuOpen = true;
    }
}

void App::handleAboutInput(const InputFrame& input) {
    if (input.isPressed(Button::Back) || input.isPressed(Button::Confirm)) screen_ = Screen::Options;
}

void App::render() {
    if (!firstFrameStarted_) {
        firstFrameStarted_ = true;
        BootDiagnostics::stage(14, "first event/input poll complete; first frame begin");
        BootDiagnostics::holdIfRequested(14, renderer_);
    }
    if (thumbnailCache_) thumbnailCache_->beginFrame();
    setDrawColor(renderer_, theme().background);
    SDL_RenderClear(renderer_);

    switch (screen_) {
        case Screen::Library:
            renderLibrary();
            break;
        case Screen::Series:
            renderChapters();
            break;
        case Screen::Reader:
            renderReader();
            break;
        case Screen::Bookmarks:
            renderBookmarks();
            break;
        case Screen::Options:
            renderOptions();
            break;
        case Screen::Theme:
            renderThemePicker();
            break;
        case Screen::About:
            renderAbout();
            break;
    }
#if MANGAPSP_HARDWARE_DIAGNOSTICS
    if (hardwareTestOverlay_) renderHardwareTestOverlay();
#endif

    SDL_RenderPresent(renderer_);
    if (!firstFramePresented_) {
        firstFramePresented_ = true;
        BootDiagnostics::stage(15, "first frame presented");
        BootDiagnostics::holdIfRequested(15, renderer_);
    }
}

void App::renderHeader(const std::string& title, const std::string& subtitle) {
    const UiTheme& palette = theme();
    drawPanel({0, 0, ScreenWidth, 30}, palette.surface);
    drawStrongText(renderer_, 12, 11, truncate(title, 34), palette.textPrimary);
    if (!subtitle.empty()) {
        const std::string right = truncate(subtitle, 22);
        drawText(renderer_, std::max(250, ScreenWidth - 12 - static_cast<int>(right.size()) * 8),
                 11, right, palette.textSecondary);
    }
    setDrawColor(renderer_, palette.accentPrimary);
    SDL_RenderDrawLine(renderer_, 0, 29, ScreenWidth, 29);
}

void App::renderFooter(const std::string& text) {
    const UiTheme& palette = theme();
    drawPanel({0, ScreenHeight - 24, ScreenWidth, 24}, palette.surface);
    setDrawColor(renderer_, palette.border);
    SDL_RenderDrawLine(renderer_, 0, ScreenHeight - 24, ScreenWidth, ScreenHeight - 24);
    drawText(renderer_, 10, ScreenHeight - 16, truncate(text, 58), palette.textSecondary);
}

void App::renderListItem(int y, const std::string& text, bool selected) {
    const UiTheme& palette = theme();
    SDL_Rect rect {10, y - 5, ScreenWidth - 20, 20};
    if (selected) {
        drawPanel(rect, palette.surfaceSelected);
        drawSelectionBorder(rect);
    }
    drawText(renderer_, 18, y, truncate(text, 54),
             selected ? palette.accentPrimary : palette.textPrimary);
}

void App::renderCenteredMessage(const std::string& line1, const std::string& line2) {
    const int x1 = std::max(8, (ScreenWidth - static_cast<int>(line1.size()) * 8) / 2);
    drawStrongText(renderer_, x1, 112, line1, theme().textPrimary);

    if (!line2.empty()) {
        const int x2 = std::max(8, (ScreenWidth - static_cast<int>(line2.size()) * 8) / 2);
        drawText(renderer_, x2, 132, line2, theme().textSecondary);
    }
}

const UiTheme& App::theme() const {
    return uiTheme(settingsManager_.settings().selectedTheme);
}

void App::drawPanel(const SDL_Rect& rect, const UiColor& color) {
    setDrawColor(renderer_, color);
    SDL_Rect copy = rect;
    SDL_RenderFillRect(renderer_, &copy);
}

void App::drawSelectionBorder(const SDL_Rect& rect) {
    setDrawColor(renderer_, theme().accentPrimary);
    SDL_Rect outer = rect;
    SDL_RenderDrawRect(renderer_, &outer);
    if (outer.w > 4 && outer.h > 4) {
        SDL_Rect inner {outer.x + 1, outer.y + 1, outer.w - 2, outer.h - 2};
        SDL_RenderDrawRect(renderer_, &inner);
    }
}

void App::drawProgressBar(const SDL_Rect& rect, float progress) {
    const UiTheme& palette = theme();
    drawPanel(rect, palette.progressBackground);
    const float clamped = std::max(0.0f, std::min(1.0f, progress));
    if (clamped > 0.0f) {
        SDL_Rect fill = rect;
        fill.w = std::max(1, static_cast<int>(static_cast<float>(rect.w) * clamped));
        drawPanel(fill, palette.progressFill);
    }
}

void App::drawCoverCard(const Series& series, const SDL_Rect& cover, bool selected) {
    const UiTheme& palette = theme();
    SDL_Rect shadow {cover.x + 3, cover.y + 3, cover.w, cover.h};
    drawPanel(shadow, palette.border);
    drawPanel(cover, palette.surface);
    if (thumbnailCache_) {
        if (SDL_Texture* texture = thumbnailCache_->get(series)) {
            SDL_Rect target = cover;
            SDL_RenderCopy(renderer_, texture, nullptr, &target);
        }
    }
    SDL_Rect border {cover.x - 2, cover.y - 2, cover.w + 4, cover.h + 4};
    if (selected) drawSelectionBorder(border);
    else {
        setDrawColor(renderer_, palette.border);
        SDL_RenderDrawRect(renderer_, &border);
    }
}

void App::renderLibrary() {
    const auto& library = libraryManager_.series();
    const UiTheme& palette = theme();
    renderHeader("NexaManga PSP", std::to_string(library.size()) + " SERIES");

    if (library.empty()) {
        renderCenteredMessage("No manga found", "Add manga to:");
        const std::string root = "/MANGA/";
        drawStrongText(renderer_, (ScreenWidth - static_cast<int>(root.size()) * 8) / 2,
                       151, root, palette.accentPrimary);
        renderFooter("X Refresh");
        return;
    }

    const ReadingProgress* recent = saveManager_.mostRecent();
    SDL_Rect continueRect {10, 35, ScreenWidth - 20, 58};
    drawPanel(continueRect, libraryState_.continueFocused ? palette.surfaceSelected : palette.surface);
    if (libraryState_.continueFocused) drawSelectionBorder(continueRect);
    drawStrongText(renderer_, 18, 40, "CONTINUE", palette.accentPrimary);
    if (recent) {
        ProgressLocation location;
        const bool resolved = saveManager_.resolve(library, *recent, location);
        if (resolved && location.seriesIndex < library.size()) {
            const Series& recentSeries = library[location.seriesIndex];
            SDL_Rect recentCover {18, 53, 24, 32};
            drawCoverCard(recentSeries, recentCover, false);
            drawStrongText(renderer_, 52, 53, truncate(recent->seriesName, 30), palette.textPrimary);
            drawText(renderer_, 52, 66,
                     truncate("Chapter " + recent->chapterName + "  •  Page " +
                              std::to_string(recent->pageIndex + 1u), 40), palette.textSecondary);
            const std::size_t pages = location.chapterIndex < recentSeries.chapters.size()
                ? recentSeries.chapters[location.chapterIndex].pageCount() : 0u;
            const float progress = chapterProgress(recent, pages);
            drawProgressBar({52, 81, 280, 5}, progress);
            drawText(renderer_, 342, 77, std::to_string(static_cast<int>(progress * 100.0f)) + "%",
                     palette.textSecondary);
        }
    } else {
        drawText(renderer_, 18, 62, "No reading history yet", palette.textSecondary);
    }

    std::size_t selectedOrder = 0;
    const auto selected = std::find(libraryOrder_.begin(), libraryOrder_.end(),
                                    libraryState_.selectedSeries);
    if (selected != libraryOrder_.end()) selectedOrder = static_cast<std::size_t>(selected - libraryOrder_.begin());
    const std::size_t first = (selectedOrder / 4u) * 4u;
    for (std::size_t column = 0; column < 4 && first + column < libraryOrder_.size(); ++column) {
        const std::size_t index = libraryOrder_[first + column];
        const Series& series = library[index];
        const int x = 18 + static_cast<int>(column) * 116;
        const bool isSelected = !libraryState_.continueFocused && index == libraryState_.selectedSeries;
        SDL_Rect cover {x, 102, 72, 96};
        drawCoverCard(series, cover, isSelected);
        drawStrongText(renderer_, x - 2, 205, truncate(series.name, 12),
                       isSelected ? palette.accentPrimary : palette.textPrimary);
        float totalProgress = 0.0f;
        for (const Chapter& chapter : series.chapters) {
            totalProgress += chapterProgress(saveManager_.find(series, chapter), chapter.pageCount());
        }
        const float progress = series.chapters.empty() ? 0.0f :
            totalProgress / static_cast<float>(series.chapters.size());
        drawProgressBar({x - 2, 221, 76, 5}, progress);
        drawText(renderer_, x - 2, 231,
                 std::to_string(static_cast<int>(progress * 100.0f)) + "%",
                 palette.textSecondary);
    }

    const std::string sortName = sortModeName(libraryState_.sortMode);
    drawText(renderer_, ScreenWidth - 10 - static_cast<int>(sortName.size()) * 8,
             235, sortName, palette.textSecondary);
    renderFooter("X Open    SQ Sort    TRI Options    O Back");
}

void App::renderChapters() {
    const auto& library = libraryManager_.series();
    if (library.empty() || libraryState_.selectedSeries >= library.size()) {
        return;
    }

    const Series& series = library[libraryState_.selectedSeries];
    const UiTheme& palette = theme();
    renderHeader("< Library", "NexaManga PSP");

    SDL_Rect coverRect {18, 46, 86, 116};
    drawCoverCard(series, coverRect, false);
    float totalProgress = 0.0f;
    const ReadingProgress* newest = nullptr;
    for (const Chapter& chapter : series.chapters) {
        if (const ReadingProgress* progress = saveManager_.find(series, chapter)) {
            totalProgress += chapterProgress(progress, chapter.pageCount());
            if (!newest || progress->lastReadOrder > newest->lastReadOrder) newest = progress;
        }
    }
    const float overall = series.chapters.empty() ? 0.0f :
        totalProgress / static_cast<float>(series.chapters.size());
    drawStrongText(renderer_, 120, 47, truncate(series.name, 38), palette.textPrimary);
    drawText(renderer_, 120, 64, std::to_string(static_cast<int>(overall * 100.0f)) + "% complete",
             palette.textSecondary);
    drawProgressBar({120, 79, 220, 6}, overall);
    drawText(renderer_, 120, 91,
             newest ? "Continue Chapter " + truncate(newest->chapterName, 22) : "Start reading",
             palette.accentPrimary);
    drawStrongText(renderer_, 120, 108, "Chapters", palette.textPrimary);

    constexpr int firstY = 126;
    constexpr int rowH = 19;
    constexpr int visibleRows = 6;

    size_t first = 0;
    if (seriesState_.selectedChapter >= visibleRows) {
        first = seriesState_.selectedChapter - visibleRows + 1;
    }

    for (int row = 0; row < visibleRows; ++row) {
        const size_t index = first + static_cast<size_t>(row);
        if (index >= series.chapters.size()) break;

        const Chapter& chapter = series.chapters[index];

        std::string status = "UNREAD";
        if (!chapter.available) status = "UNAVAILABLE";
        if (const ReadingProgress* progress = saveManager_.find(series, chapter)) {
            if (progress->completed) status = "COMPLETE";
            else status = std::to_string(progress->pageIndex + 1u) + " / " +
                          std::to_string(chapter.pageCount());
        }

        const bool selectedRow = index == seriesState_.selectedChapter;
        SDL_Rect rowRect {116, firstY + row * rowH - 5, 352, 18};
        if (selectedRow) {
            drawPanel(rowRect, palette.surfaceSelected);
            drawSelectionBorder(rowRect);
        }
        drawText(renderer_, 124, firstY + row * rowH, truncate(chapter.name, 24),
                 selectedRow ? palette.accentPrimary : palette.textPrimary);
        drawText(renderer_, 376, firstY + row * rowH, truncate(status, 11), palette.textSecondary);
    }

    if (!lastError_.empty()) {
        drawText(renderer_, 12, ScreenHeight - 36, truncate(lastError_, 56), palette.accentSecondary);
    }

    renderFooter("X Read       TRI Options       O Back");
}

void App::renderReader() {
    reader_->render();

    const Uint32 now = SDL_GetTicks();
    const bool showHud = static_cast<Sint32>(readerHudUntil_ - now) > 0;
    if (showHud) {
        const UiTheme& palette = theme();
        UiColor hudSurface = palette.surface;
        hudSurface.a = 225;
        drawPanel({0, 0, ScreenWidth, 34}, hudSurface);
        std::string chapterName;
        std::string seriesName;
        const auto& library = libraryManager_.series();
        if (libraryState_.selectedSeries < library.size() &&
            seriesState_.selectedChapter < library[libraryState_.selectedSeries].chapters.size()) {
            seriesName = library[libraryState_.selectedSeries].name;
            chapterName = library[libraryState_.selectedSeries].chapters[seriesState_.selectedChapter].name;
        }
        drawStrongText(renderer_, 8, 5,
                       truncate(seriesName + "  •  Ch." + chapterName, 42), palette.textPrimary);
        const std::string pages = pageStatus(reader_->currentPageIndex(), reader_->pageCount());
        drawText(renderer_, ScreenWidth - 8 - static_cast<int>(pages.size()) * 8, 5,
                 pages, palette.textPrimary);
        std::ostringstream mode;
        mode.precision(2);
        const char* direction = reader_->readingDirection() == ReadingDirection::RightToLeft ? "RTL" : "LTR";
        mode << (reader_->fitMode() == MangaReader::FitMode::Page ? "Fit Page" : "Fit Width")
             << "  •  " << std::fixed << reader_->zoom() << "x  •  " << direction;
        if (reader_->smartReading()) mode << "  •  Smart";
        drawText(renderer_, 8, 20, truncate(mode.str(), 57), palette.textSecondary);

        drawPanel({0, ScreenHeight - 20, ScreenWidth, 20}, hudSurface);
        drawText(renderer_, 8, ScreenHeight - 14, "L/R Page      X Zoom      SELECT Menu", palette.textSecondary);
    }

#if MANGAPSP_HARDWARE_DIAGNOSTICS
    if (debugOverlay_) {
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 190);
        SDL_Rect debugRect {0, showHud ? 34 : 0, ScreenWidth, 68};
        SDL_RenderFillRect(renderer_, &debugRect);

        std::ostringstream memory;
        memory << reader_->sourceWidth() << "x" << reader_->sourceHeight()
               << " cur " << reader_->imageMemoryBytes() / 1024u << "K"
               << " prev " << reader_->previousPageMemoryBytes() / 1024u << "K"
               << " next " << reader_->nextPageMemoryBytes() / 1024u << "K"
               << " view " << reader_->viewportTextureMemoryBytes() / 1024u << "K"
               << " total " << reader_->totalTrackedMemoryBytes() / 1024u << "K/"
               << reader_->memoryBudgetBytes() / 1024u << "K";
        std::ostringstream transient;
        transient << "CBZ " << reader_->lastCompressedBytes() / 1024u << "K"
                  << " row " << reader_->lastScanlineBytes() / 1024u << "K"
                  << " lib~ " << reader_->lastDecoderEstimateBytes() / 1024u << "K"
                  << " tracked-peak " << reader_->lastTrackedDecodePeakBytes() / 1024u << "K"
                  << " predicted " << reader_->lastDecodePeakBytes() / 1024u << "K";
        const DecodeTiming& timing = reader_->lastDecodeTiming();
        const ImageLoaderMetrics& imageMetrics = reader_->imageLoaderMetrics();
        const std::size_t successful = imageMetrics.successfulLoads;
        const std::uint64_t averageMicros = successful
            ? imageMetrics.totalDecodeMicros / successful : 0;
        std::ostringstream decode;
        decode << "us read " << timing.sourceReadMicros
               << " probe " << timing.headerProbeMicros
               << " dec " << (timing.jpegDecodeMicros + timing.fallbackDecodeMicros)
               << " 565 " << timing.rgb565OutputMicros
               << " total " << timing.totalMicros
               << " | direct/fallback/fail " << imageMetrics.directJpegDecodes << "/"
               << imageMetrics.pngFallbackDecodes << "/" << imageMetrics.jpegDecodeFailures
               << " avg/peak " << averageMicros << "/" << imageMetrics.peakDecodeMicros;
        const PageCacheStats& stats = reader_->cacheStats();
        std::ostringstream cache;
        cache << "entries " << reader_->cachedPageCount()
              << " hit " << stats.hits << " miss " << stats.misses
              << " evict " << stats.evictions
              << " preload " << stats.preloadLoads << "/" << stats.preloadFailures
              << " budget-refuse " << stats.preloadBudgetRejections;
        drawText(renderer_, 8, showHud ? 39 : 5,
                  truncate(memory.str(), 76), 180, 230, 190, 255);
        drawText(renderer_, 8, showHud ? 55 : 21,
                  truncate(transient.str(), 76), 180, 230, 190, 255);
        drawText(renderer_, 8, showHud ? 71 : 37,
                  truncate(decode.str(), 76), 180, 230, 190, 255);
        drawText(renderer_, 8, showHud ? 87 : 53,
                  truncate(cache.str(), 76), 180, 230, 190, 255);
    }
#endif

    if (!reader_->lastError().empty()) {
        setDrawColor(renderer_, theme().accentSecondary);
        SDL_Rect errorRect {20, 96, ScreenWidth - 40, 72};
        SDL_RenderFillRect(renderer_, &errorRect);
        drawText(renderer_, 30, 108, "Page unavailable:", 255, 255, 255, 255);
        drawText(renderer_, 30, 124, truncate(reader_->lastError(), 50), 255, 220, 220, 255);
        drawText(renderer_, 30, 146, "L/R Try another page   O Back", 255, 235, 205, 255);
    }
    if (readerOverlay_.menuOpen) renderReaderMenu();
}

void App::renderReaderMenu() {
    const UiTheme& palette = theme();
    UiColor panelColor = palette.surface;
    panelColor.a = 242;
    setDrawColor(renderer_, panelColor);
    SDL_Rect panel {78, 25, 324, 222};
    SDL_RenderFillRect(renderer_, &panel);
    drawSelectionBorder(panel);
    drawStrongText(renderer_, 94, 38, "READER MENU", palette.accentPrimary);

    const char* direction = reader_->readingDirection() == ReadingDirection::RightToLeft
        ? "RTL MANGA" : "LTR COMIC";
    const char* spread = reader_->spreadMode() == SpreadMode::Auto ? "AUTO" :
                         reader_->spreadMode() == SpreadMode::FullSpread ? "FULL" : "SPLIT";
    std::vector<std::string> items {
        std::string("Reading Direction     ") + direction,
        std::string("Smart Reading         ") + (reader_->smartReading() ? "ON" : "OFF"),
        std::string("Spread Mode           ") + spread,
        std::string("Fit Mode              ") +
            (reader_->fitMode() == MangaReader::FitMode::Page ? "PAGE" : "WIDTH"),
        std::string("Theme                 ") + theme().name,
        "Add Bookmark",
        "Bookmarks",
        "Chapter List"
    };
#if MANGAPSP_HARDWARE_DIAGNOSTICS
    items.push_back(std::string("Debug Overlay         ") + (debugOverlay_ ? "ON" : "OFF"));
    items.push_back("Hardware Test");
#endif
    for (std::size_t i = 0; i < items.size(); ++i) {
        const int y = 58 + static_cast<int>(i) * 18;
        if (i == readerOverlay_.menuSelection) {
            setDrawColor(renderer_, palette.surfaceSelected);
            SDL_Rect selected {90, y - 5, 300, 18};
            SDL_RenderFillRect(renderer_, &selected);
            drawSelectionBorder(selected);
        }
        drawText(renderer_, 98, y, items[i],
                 i == readerOverlay_.menuSelection ? palette.accentPrimary : palette.textPrimary);
    }
    drawText(renderer_, 94, 232, "X Change / Open     O Close", palette.textSecondary);
}

void App::renderHardwareTestOverlay() {
#if MANGAPSP_HARDWARE_DIAGNOSTICS
    SDL_SetRenderDrawColor(renderer_, 8, 11, 14, 248);
    SDL_Rect panel {8, 8, ScreenWidth - 16, ScreenHeight - 16};
    SDL_RenderFillRect(renderer_, &panel);
    drawText(renderer_, 18, 18, "NexaManga PSP HARDWARE TEST (measured values)",
             137, 220, 190, 255);

    const Platform::HardwareInfo hardware = Platform::hardwareInfo();
    const PageCacheStats& cache = reader_->cacheStats();
    const DecodeTiming& timing = reader_->lastDecodeTiming();
    std::vector<std::string> lines;
    std::ostringstream line;
    line << "Build " << (MANGAPSP_PSP_RC ? "PSP RC" : "DEVELOPMENT")
         << " | " << hardware.platform << " | " << hardware.memoryProfile;
    lines.push_back(line.str()); line.str(""); line.clear();
    line << "Free user memory " << (hardware.freeMemoryMeasured
        ? std::to_string(hardware.freeUserMemoryBytes / 1024u) + " KiB" : "n/a")
         << " | image budget " << reader_->memoryBudgetBytes() / 1024u << " KiB";
    lines.push_back(line.str()); line.str(""); line.clear();
    line << "Page " << reader_->currentPageIndex() + 1u << "/" << reader_->pageCount()
         << " | " << reader_->sourceWidth() << "x" << reader_->sourceHeight();
    lines.push_back(line.str()); line.str(""); line.clear();
    line << "Memory current/previous/next/viewport " << reader_->imageMemoryBytes() / 1024u
         << "/" << reader_->previousPageMemoryBytes() / 1024u << "/"
         << reader_->nextPageMemoryBytes() / 1024u << "/"
         << reader_->viewportTextureMemoryBytes() / 1024u << " KiB";
    lines.push_back(line.str()); line.str(""); line.clear();
    line << "Cache entries/hit/miss/evict " << reader_->cachedPageCount() << "/"
         << cache.hits << "/" << cache.misses << "/" << cache.evictions
         << " | tracked " << reader_->totalTrackedMemoryBytes() / 1024u << " KiB";
    lines.push_back(line.str()); line.str(""); line.clear();
    line << "CBZ compressed " << reader_->lastCompressedBytes() / 1024u
         << " KiB | scanline " << reader_->lastScanlineBytes() / 1024u << " KiB";
    lines.push_back(line.str()); line.str(""); line.clear();
    line << "Decode peak tracked/predicted " << reader_->lastTrackedDecodePeakBytes() / 1024u
         << "/" << reader_->lastDecodePeakBytes() / 1024u << " KiB";
    lines.push_back(line.str()); line.str(""); line.clear();
    line << "Decode us read/probe/decode/565/total " << timing.sourceReadMicros << "/"
         << timing.headerProbeMicros << "/"
         << timing.jpegDecodeMicros + timing.fallbackDecodeMicros << "/"
         << timing.rgb565OutputMicros << "/" << timing.totalMicros;
    lines.push_back(line.str()); line.str(""); line.clear();
    line << "Library/chapter open " << lastLibraryOpenMicros_ << "/"
         << lastChapterOpenMicros_ << " us";
    lines.push_back(line.str()); line.str(""); line.clear();
    line << "Page switch " << lastPageSwitchMicros_ << " us | frame "
         << static_cast<unsigned int>(frameSeconds_ * 1000000.0f) << " us | FPS "
         << (frameSeconds_ > 0.0f ? 1.0f / frameSeconds_ : 0.0f);
    lines.push_back(line.str()); line.str(""); line.clear();
    if (thumbnailCache_) {
        line << "Thumb entries/bytes/gen ok/try/last/peak us " << thumbnailCache_->entryCount()
             << "/" << thumbnailCache_->residentBytes() << "/"
             << thumbnailCache_->generationSuccesses() << "/"
             << thumbnailCache_->generationAttempts() << "/"
             << thumbnailCache_->lastGenerationMicros() << "/"
             << thumbnailCache_->peakGenerationMicros();
        lines.push_back(line.str()); line.str(""); line.clear();
    }
    line << "Preload stage/in-progress/complete/attempt " << reader_->preloadStage() << "/"
         << (reader_->preloadInProgress() ? "yes" : "no") << "/"
         << reader_->preloadCompletions() << "/" << reader_->preloadAttempts();
    lines.push_back(line.str()); line.str(""); line.clear();
    line << "Suspend/resume events " << suspendCount_ << "/" << resumeCount_
         << " | source suspended " << (reader_->suspended() ? "yes" : "no");
    lines.push_back(line.str());

    for (std::size_t i = 0; i < lines.size(); ++i) {
        drawText(renderer_, 18, 42 + static_cast<int>(i) * 15, truncate(lines[i], 56),
                 220, 228, 232, 255);
    }
    drawText(renderer_, 18, 242, "X / O / SELECT Close", 160, 180, 190, 255);
#endif
}

void App::renderBookmarks() {
    renderHeader("Bookmarks", std::to_string(saveManager_.bookmarks().size()) + " SAVED POSITIONS");
    const auto& bookmarks = saveManager_.bookmarks();
    if (bookmarks.empty()) {
        renderCenteredMessage("No bookmarks yet.", "Add one from the reader menu.");
        renderFooter("O Back");
        return;
    }
    constexpr std::size_t visible = 8;
    const std::size_t first = bookmarkState_.selection >= visible
        ? bookmarkState_.selection - visible + 1u : 0u;
    for (std::size_t row = 0; row < visible && first + row < bookmarks.size(); ++row) {
        const std::size_t index = first + row;
        const Bookmark& bookmark = bookmarks[index];
        std::ostringstream label;
        label << bookmark.seriesName << "  •  " << bookmark.label;
        renderListItem(48 + static_cast<int>(row) * 22, label.str(),
                       first + row == bookmarkState_.selection);
    }
    if (!lastError_.empty()) drawText(renderer_, 12, 229, truncate(lastError_, 56), theme().accentSecondary);
    renderFooter("X Open Bookmark   O Back");
}

void App::renderOptions() {
    const UiTheme& palette = theme();
    renderHeader("Options", "NexaManga PSP");
    SDL_Rect panel {54, 43, 372, 176};
    drawPanel(panel, palette.surface);
    setDrawColor(renderer_, palette.border);
    SDL_RenderDrawRect(renderer_, &panel);

    const char* direction = settingsManager_.settings().defaultDirection == ReadingDirection::RightToLeft
        ? "RTL" : "LTR";
    const std::string labels[] = {
        "Theme",
        "HUD Timeout",
        "Reading Direction",
        "About NexaManga PSP",
#if MANGAPSP_HARDWARE_DIAGNOSTICS
        "Debug Overlay"
#endif
    };
    const std::string values[] = {
        std::string(theme().name) + "  >",
        std::to_string(settingsManager_.settings().hudTimeoutMs / 1000.0f).substr(0, 3) + " sec",
        direction,
        "",
#if MANGAPSP_HARDWARE_DIAGNOSTICS
        debugOverlay_ ? "ON" : "OFF"
#endif
    };
    const std::size_t count = sizeof(labels) / sizeof(labels[0]);
    for (std::size_t i = 0; i < count; ++i) {
        const int y = 59 + static_cast<int>(i) * 29;
        SDL_Rect row {66, y - 7, 348, 24};
        if (i == optionsState_.selection) {
            drawPanel(row, palette.surfaceSelected);
            drawSelectionBorder(row);
        }
        drawText(renderer_, 76, y, labels[i],
                 i == optionsState_.selection ? palette.accentPrimary : palette.textPrimary);
        if (!values[i].empty()) {
            drawText(renderer_, 404 - static_cast<int>(values[i].size()) * 8, y,
                     values[i], palette.textSecondary);
        }
    }
    renderFooter("X Change / Open                         O Back");
}

void App::renderThemePicker() {
    const UiTheme& palette = theme();
    renderHeader("Theme", "Live Preview");
    SDL_Rect panel {90, 47, 300, 164};
    drawPanel(panel, palette.surface);
    setDrawColor(renderer_, palette.border);
    SDL_RenderDrawRect(renderer_, &panel);
    for (std::size_t i = 0; i < uiThemeCount(); ++i) {
        const UiTheme& option = uiTheme(uiThemeIdAt(i));
        const int y = 65 + static_cast<int>(i) * 28;
        SDL_Rect row {102, y - 7, 276, 23};
        if (i == themeState_.selection) {
            drawPanel(row, palette.surfaceSelected);
            drawSelectionBorder(row);
        }
        drawText(renderer_, 114, y, std::string(i == themeState_.selection ? "> " : "  ") + option.name,
                 i == themeState_.selection ? palette.accentPrimary : palette.textPrimary);
        SDL_Rect swatch {344, y - 2, 18, 8};
        drawPanel(swatch, option.accentPrimary);
    }
    renderFooter("UP/DOWN Preview       X Apply       O Back");
}

void App::renderAbout() {
    const UiTheme& palette = theme();
    renderHeader("About", "NexaManga PSP");
    SDL_Rect panel {46, 49, 388, 158};
    drawPanel(panel, palette.surface);
    setDrawColor(renderer_, palette.border);
    SDL_RenderDrawRect(renderer_, &panel);
    drawStrongText(renderer_, 70, 68, "NexaManga PSP", palette.accentPrimary);
    drawText(renderer_, 70, 91, "A controller-first manga reader for PSP.", palette.textPrimary);
    drawText(renderer_, 70, 111, "Folder manga  •  JPG/PNG  •  CBZ", palette.textSecondary);
    drawText(renderer_, 70, 129, "Bookmarks  •  Continue Reading  •  Smart Reading", palette.textSecondary);
    drawText(renderer_, 70, 157, "Designed for the PSP 480x272 display.", palette.textPrimary);
    renderFooter("X / O Back");
}

std::string App::truncate(const std::string& value, size_t maxChars) {
    if (value.size() <= maxChars) return value;
    if (maxChars <= 3) return value.substr(0, maxChars);
    return value.substr(0, maxChars - 3) + "...";
}
