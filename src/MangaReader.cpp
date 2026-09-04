#include "MangaReader.hpp"

#include "Log.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <utility>

namespace {

float clampFloat(float value, float low, float high) {
    return std::max(low, std::min(value, high));
}

} // namespace

MangaReader::MangaReader(SDL_Renderer* renderer, int screenWidth, int screenHeight,
                         std::size_t memoryBudgetBytes)
    : renderer_(renderer),
      screenWidth_(screenWidth),
      screenHeight_(screenHeight),
      imageLoader_(memoryBudgetBytes),
      cache_(memoryBudgetBytes, 0) {
    viewport_.reset(SDL_CreateRGBSurfaceWithFormat(
        0, screenWidth_, screenHeight_, 16, SDL_PIXELFORMAT_RGB565));
    if (!viewport_) {
        lastError_ = std::string("Viewport allocation failed: ") + SDL_GetError();
        return;
    }

    viewportTexture_.reset(SDL_CreateTexture(
        renderer_, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
        screenWidth_, screenHeight_));
    if (!viewportTexture_) {
        lastError_ = std::string("Viewport texture creation failed: ") + SDL_GetError();
    }
    cache_.configure(memoryBudgetBytes, fixedMemoryBytes());
}

bool MangaReader::isReady() const {
    return renderer_ && viewport_ && viewportTexture_;
}

bool MangaReader::openChapter(const Chapter* chapter, std::size_t startPage) {
    close();
    if (!chapter) {
        lastError_ = "No chapter was selected.";
        return false;
    }
    if (!isReady()) {
        if (lastError_.empty()) lastError_ = "Reader rendering resources are unavailable.";
        return false;
    }

    chapter_ = chapter;
    completionReached_ = false;
    pageSource_ = createPageSource(*chapter_);
    if (!pageSource_ || !pageSource_->available() || pageSource_->pageCount() == 0) {
        lastError_ = pageSource_ ? pageSource_->lastError() : "Could not create page source.";
        chapter_ = nullptr;
        pageSource_.reset();
        return false;
    }

    startPage = std::min(startPage, pageSource_->pageCount() - 1);
    for (std::size_t offset = 0; offset < pageSource_->pageCount(); ++offset) {
        const std::size_t candidate = (startPage + offset) % pageSource_->pageCount();
        if (loadPage(candidate)) return true;
        failedPages_.insert(candidate);
    }

    chapter_ = nullptr;
    if (lastError_.empty()) lastError_ = "Chapter has no decodable pages.";
    return false;
}

void MangaReader::close() {
    releasePage();
    cache_.clear();
    pageSource_.reset();
    chapter_ = nullptr;
    pageIndex_ = 0;
    failedPages_.clear();
    lastDecodePeakBytes_ = 0;
    lastTrackedDecodePeakBytes_ = 0;
    lastDecoderEstimateBytes_ = 0;
    lastCompressedBytes_ = 0;
    lastScanlineBytes_ = 0;
    lastDecodeTiming_ = {};
    lastLoadFailure_ = ImageLoadFailure::None;
    preloadStage_ = 0;
    preloadAttempts_ = 0;
    preloadCompletions_ = 0;
    preloadInProgress_ = false;
    suspended_ = false;
    logicalStops_.clear();
    logicalIndex_ = 0;
    manualPanNeedsResync_ = false;
    completionReached_ = false;
    lastError_.clear();
}

std::size_t MangaReader::pageCount() const {
    return pageSource_ ? pageSource_->pageCount() : 0;
}

std::size_t MangaReader::fixedMemoryBytes() const {
    const std::size_t viewportSurfaceBytes = ImageLoader::bytesFor(viewport_.get());
    const std::size_t estimatedTextureBytes =
        static_cast<std::size_t>(screenWidth_) * static_cast<std::size_t>(screenHeight_) * 2u;
    return viewportSurfaceBytes + estimatedTextureBytes;
}

std::size_t MangaReader::totalTrackedMemoryBytes() const {
    return cache_.totalTrackedBytes();
}

void MangaReader::releasePage() {
    source_.reset();
    sourceMemoryBytes_ = 0;
    dirty_ = true;
}

void MangaReader::recordLoadDiagnostics(const ImageLoadResult& loaded) {
    lastDecodePeakBytes_ = loaded.estimatedPeakBytes;
    lastTrackedDecodePeakBytes_ = loaded.trackedPeakBytes;
    lastDecoderEstimateBytes_ = loaded.estimatedLibraryOverheadBytes;
    lastCompressedBytes_ = loaded.encodedBytes;
    lastScanlineBytes_ = loaded.info.scanlineWorkspaceBytes;
    lastDecodeTiming_ = loaded.timing;
}

bool MangaReader::loadPage(std::size_t index, bool preload) {
    if (!pageSource_ || index >= pageSource_->pageCount()) return false;

    if (std::shared_ptr<SDL_Surface> cached = cache_.get(index)) {
        MANGAPSP_LOG_INFO("Page cache hit: " + pageSource_->pageId(index));
        if (preload) return true;
        source_ = std::move(cached);
        sourceMemoryBytes_ = ImageLoader::bytesFor(source_.get());
        pageIndex_ = index;
        failedPages_.erase(index);
        lastLoadFailure_ = ImageLoadFailure::None;
        preloadStage_ = 0;
        const std::size_t evictionsBefore = cache_.stats().evictions;
        cache_.rebalance(pageIndex_, pageSource_->pageCount());
        if (cache_.stats().evictions != evictionsBefore) {
            MANGAPSP_LOG_INFO("Page cache rebalanced after cached navigation.");
        }
        resetView();
        lastError_.clear();
        return true;
    }
    MANGAPSP_LOG_INFO("Page cache miss: " + pageSource_->pageId(index));
    if (preload) MANGAPSP_LOG_INFO("Preload attempt: " + pageSource_->pageId(index));

    ImageLoadResult loaded = imageLoader_.loadRgb565(
        *pageSource_, index, cache_.totalTrackedBytes());
    recordLoadDiagnostics(loaded);
    if (!loaded.surface) {
        if (!preload && loaded.failure == ImageLoadFailure::Budget && cache_.entryCount() > 1) {
            Log::warning("Required page missed budget; evicting non-current cached pages and retrying.");
            cache_.trimToCurrent(pageIndex_);
            loaded = imageLoader_.loadRgb565(*pageSource_, index, cache_.totalTrackedBytes());
            recordLoadDiagnostics(loaded);
        }
    }
    if (!loaded.surface) {
        lastLoadFailure_ = loaded.failure;
        if (preload) {
            cache_.recordPreloadFailure(loaded.failure == ImageLoadFailure::Budget);
            Log::warning("Preload failed: " + pageSource_->pageId(index) + ": " + loaded.error);
            return false;
        }
        lastError_ = loaded.error;
        return false;
    }

    const std::size_t protectedIndex = source_
        ? pageIndex_ : std::numeric_limits<std::size_t>::max();
    const std::size_t evictionsBefore = cache_.stats().evictions;
    if (!cache_.insert(index, loaded.surface, loaded.surfaceBytes, protectedIndex,
                       pageSource_->pageCount(), preload)) {
        lastLoadFailure_ = ImageLoadFailure::Budget;
        if (preload) {
            cache_.recordPreloadFailure();
            Log::warning("Preload refused by page-cache budget: " + pageSource_->pageId(index));
            return false;
        }
        lastError_ = "Decoded page could not fit in the bounded page cache.";
        return false;
    }
    if (cache_.stats().evictions != evictionsBefore) {
        MANGAPSP_LOG_INFO("Page cache evicted an older decoded page.");
    }
    if (preload) {
        MANGAPSP_LOG_INFO("Preload complete: " + pageSource_->pageId(index));
        return true;
    }

    source_ = std::move(loaded.surface);
    sourceMemoryBytes_ = loaded.surfaceBytes;
    pageIndex_ = index;
    failedPages_.erase(index);
    lastLoadFailure_ = ImageLoadFailure::None;
    preloadStage_ = 0;
    cache_.rebalance(pageIndex_, pageSource_->pageCount());
    resetView();
    lastError_.clear();
    return true;
}

bool MangaReader::navigate(int direction) {
    if (!chapter_ || direction == 0) return false;

    const std::size_t count = pageSource_->pageCount();
    std::size_t candidate = pageIndex_;
    while (true) {
        if (direction > 0) {
            if (candidate + 1 >= count) return false;
            ++candidate;
        } else {
            if (candidate == 0) return false;
            --candidate;
        }

        if (failedPages_.find(candidate) != failedPages_.end()) continue;
        if (loadPage(candidate)) return true;

        if (lastLoadFailure_ == ImageLoadFailure::Budget) return false;

        failedPages_.insert(candidate);
        Log::warning("Skipping unreadable page during navigation.");
    }
}

bool MangaReader::nextPage() {
    const bool moved = navigate(1);
    if (moved && ReaderExperience::shouldMarkChapterCompleted(pageIndex_, pageCount(), true)) {
        completionReached_ = true;
    }
    if (!moved && pageCount() > 0 && pageIndex_ + 1u >= pageCount()) {
        completionReached_ = true;
    }
    return moved;
}

bool MangaReader::previousPage() {
    return navigate(-1);
}

bool MangaReader::advanceReading() {
    if (!source_) return false;
    if (manualPanNeedsResync_ && !logicalStops_.empty()) {
        logicalIndex_ = ReaderExperience::nearestStop(
            logicalStops_, panX_, panY_, logicalStops_[logicalIndex_].spreadHalf);
        manualPanNeedsResync_ = false;
    }
    const bool useLogical = smartReading_ || splitSpreadActive();
    if (useLogical && logicalIndex_ + 1u < logicalStops_.size()) {
        applyLogicalStop(logicalIndex_ + 1u);
        if (logicalIndex_ + 1u >= logicalStops_.size() && pageIndex_ + 1u >= pageCount()) {
            completionReached_ = true;
        }
        return true;
    }
    return nextPage();
}

bool MangaReader::retreatReading() {
    if (!source_) return false;
    if (manualPanNeedsResync_ && !logicalStops_.empty()) {
        logicalIndex_ = ReaderExperience::nearestStop(
            logicalStops_, panX_, panY_, logicalStops_[logicalIndex_].spreadHalf);
        manualPanNeedsResync_ = false;
    }
    const bool useLogical = smartReading_ || splitSpreadActive();
    if (useLogical && logicalIndex_ > 0) {
        applyLogicalStop(logicalIndex_ - 1u);
        return true;
    }
    if (!previousPage()) return false;
    if ((smartReading_ || splitSpreadActive()) && !logicalStops_.empty()) {
        applyLogicalStop(logicalStops_.size() - 1u);
    }
    return true;
}

bool MangaReader::preloadOne() {
    if (!pageSource_ || !source_ || suspended_) return false;
    const std::size_t count = pageSource_->pageCount();

    while (preloadStage_ < 2) {
        std::size_t candidate = count;
        if (preloadStage_ == 0 && pageIndex_ + 1 < count) candidate = pageIndex_ + 1;
        if (preloadStage_ == 1 && pageIndex_ > 0) candidate = pageIndex_ - 1;
        ++preloadStage_;
        if (candidate >= count || failedPages_.find(candidate) != failedPages_.end() ||
            cache_.contains(candidate)) {
            continue;
        }
        ++preloadAttempts_;
        preloadInProgress_ = true;
        const bool loaded = loadPage(candidate, true);
        preloadInProgress_ = false;
        if (loaded) ++preloadCompletions_;
        return loaded;
    }
    return false;
}

void MangaReader::prepareForSuspend() {
    suspended_ = true;
    preloadStage_ = 2;
    preloadInProgress_ = false;
    if (pageSource_) pageSource_->prepareForSuspend();
}

bool MangaReader::resumeAfterSuspend() {
    bool sourceReady = true;
    if (pageSource_) sourceReady = pageSource_->resumeAfterSuspend();
    suspended_ = false;
    preloadStage_ = 0;
    dirty_ = true;
    if (!sourceReady) {
        lastError_ = pageSource_ && !pageSource_->lastError().empty()
            ? "Resume could not reopen source: " + pageSource_->lastError()
            : "Resume could not reopen the page source.";
    }
    return sourceReady;
}

void MangaReader::resetView() {
    if (!source_) return;

    const SourceBounds bounds = activeBounds();
    const float fitPageX = static_cast<float>(screenWidth_) / bounds.width;
    const float fitPageY = static_cast<float>(screenHeight_) / bounds.height;
    const float fitPage = std::min(fitPageX, fitPageY);
    const float fitWidth = fitPageX;

    minZoom_ = fitPage;
    zoom_ = fitMode_ == FitMode::Page ? fitPage : fitWidth;
    maxZoom_ = std::max(2.0f, zoom_);
    panX_ = bounds.x;
    panY_ = bounds.y;
    rebuildLogicalStops();
    if (!logicalStops_.empty()) applyLogicalStop(0);
    else clampPan();
    dirty_ = true;
}

SourceBounds MangaReader::activeBounds() const {
    if (!source_) return {};
    if (!splitSpreadActive()) {
        return {0.0f, 0.0f, static_cast<float>(source_->w), static_cast<float>(source_->h)};
    }
    const std::vector<SourceBounds> halves = ReaderExperience::spreadBounds(
        static_cast<std::uint32_t>(source_->w), static_cast<std::uint32_t>(source_->h), direction_);
    const std::uint8_t half = !logicalStops_.empty() && logicalIndex_ < logicalStops_.size()
        ? logicalStops_[logicalIndex_].spreadHalf : 0;
    return halves[std::min<std::size_t>(half, halves.size() - 1u)];
}

bool MangaReader::splitSpreadActive() const {
    return source_ && ReaderExperience::usesSplitSpread(
        spreadMode_, static_cast<std::uint32_t>(source_->w),
        static_cast<std::uint32_t>(source_->h));
}

bool MangaReader::likelySpread() const {
    return source_ && ReaderExperience::isLikelySpread(
        static_cast<std::uint32_t>(source_->w), static_cast<std::uint32_t>(source_->h));
}

void MangaReader::rebuildLogicalStops(std::size_t preferred) {
    if (!source_) return;
    logicalStops_ = ReaderExperience::buildStops(
        static_cast<std::uint32_t>(source_->w), static_cast<std::uint32_t>(source_->h),
        static_cast<std::uint32_t>(screenWidth_), static_cast<std::uint32_t>(screenHeight_),
        zoom_, direction_, spreadMode_);
    logicalIndex_ = logicalStops_.empty() ? 0 : std::min(preferred, logicalStops_.size() - 1u);
    manualPanNeedsResync_ = false;
}

void MangaReader::applyLogicalStop(std::size_t index) {
    if (logicalStops_.empty()) return;
    logicalIndex_ = std::min(index, logicalStops_.size() - 1u);
    panX_ = logicalStops_[logicalIndex_].panX;
    panY_ = logicalStops_[logicalIndex_].panY;
    manualPanNeedsResync_ = false;
    clampPan();
    dirty_ = true;
}

bool MangaReader::restoreLogicalPosition(std::size_t position) {
    if (logicalStops_.empty()) return position == 0;
    applyLogicalStop(std::min(position, logicalStops_.size() - 1u));
    return position < logicalStops_.size();
}

void MangaReader::clampPan() {
    if (!source_ || zoom_ <= 0.0f) return;

    const float visibleWidth = static_cast<float>(screenWidth_) / zoom_;
    const float visibleHeight = static_cast<float>(screenHeight_) / zoom_;
    const SourceBounds bounds = activeBounds();
    const float maxX = bounds.x + std::max(0.0f, bounds.width - visibleWidth);
    const float maxY = bounds.y + std::max(0.0f, bounds.height - visibleHeight);
    panX_ = clampFloat(panX_, bounds.x, maxX);
    panY_ = clampFloat(panY_, bounds.y, maxY);
}

void MangaReader::zoomIn() {
    if (!source_) return;

    const float oldZoom = zoom_;
    zoom_ = std::min(maxZoom_, zoom_ * 1.35f);
    if (std::abs(oldZoom - zoom_) < 0.0001f) return;

    const float centerX = panX_ + static_cast<float>(screenWidth_) / oldZoom * 0.5f;
    const float centerY = panY_ + static_cast<float>(screenHeight_) / oldZoom * 0.5f;
    panX_ = centerX - static_cast<float>(screenWidth_) / zoom_ * 0.5f;
    panY_ = centerY - static_cast<float>(screenHeight_) / zoom_ * 0.5f;
    clampPan();
    rebuildLogicalStops(ReaderExperience::nearestStop(logicalStops_, panX_, panY_));
    dirty_ = true;
}

void MangaReader::smartZoomIn() {
    if (!source_) return;
    const SourceBounds bounds = activeBounds();
    const float fitPage = std::min(static_cast<float>(screenWidth_) / bounds.width,
                                   static_cast<float>(screenHeight_) / bounds.height);
    const float fitWidth = static_cast<float>(screenWidth_) / bounds.width;
    if (zoom_ < fitWidth - 0.001f || std::abs(zoom_ - fitPage) < 0.001f) {
        zoom_ = std::min(maxZoom_, fitWidth);
        fitMode_ = FitMode::Width;
        panX_ = bounds.x;
        panY_ = bounds.y;
        rebuildLogicalStops();
        if (!logicalStops_.empty()) applyLogicalStop(0);
        dirty_ = true;
        return;
    }
    zoomIn();
}

void MangaReader::zoomOut() {
    if (!source_) return;
    zoom_ = std::max(minZoom_, zoom_ / 1.35f);
    clampPan();
    rebuildLogicalStops(ReaderExperience::nearestStop(logicalStops_, panX_, panY_));
    dirty_ = true;
}

void MangaReader::toggleFitMode() {
    setFitMode(fitMode_ == FitMode::Page ? FitMode::Width : FitMode::Page);
}

void MangaReader::setFitMode(FitMode mode) {
    if (fitMode_ == mode) return;
    fitMode_ = mode;
    resetView();
}

void MangaReader::pan(float screenDx, float screenDy) {
    if (!source_ || zoom_ <= 0.0f) return;
    const float oldX = panX_;
    const float oldY = panY_;
    panX_ += screenDx / zoom_;
    panY_ += screenDy / zoom_;
    clampPan();
    if (std::abs(oldX - panX_) > 0.001f || std::abs(oldY - panY_) > 0.001f) {
        dirty_ = true;
        manualPanNeedsResync_ = true;
    }
}

void MangaReader::setReadingDirection(ReadingDirection direction) {
    if (direction_ == direction) return;
    direction_ = direction;
    resetView();
}

void MangaReader::setSmartReading(bool enabled) {
    smartReading_ = enabled;
    if (source_) {
        rebuildLogicalStops(logicalIndex_);
        if (!logicalStops_.empty()) applyLogicalStop(logicalIndex_);
    }
}

void MangaReader::setSpreadMode(SpreadMode mode) {
    if (spreadMode_ == mode) return;
    spreadMode_ = mode;
    resetView();
}

void MangaReader::rebuildViewport() {
    if (!dirty_ || !source_ || !viewport_ || !viewportTexture_) return;

    SDL_FillRect(viewport_.get(), nullptr,
                 SDL_MapRGB(viewport_->format, 255, 255, 255));

    const SourceBounds bounds = activeBounds();
    const float scaledWidth = bounds.width * zoom_;
    const float scaledHeight = bounds.height * zoom_;
    SDL_Rect sourceRect {};
    SDL_Rect destinationRect {};

    if (scaledWidth <= static_cast<float>(screenWidth_)) {
        sourceRect.x = static_cast<int>(bounds.x);
        sourceRect.w = static_cast<int>(bounds.width);
        destinationRect.x = static_cast<int>((screenWidth_ - scaledWidth) * 0.5f);
        destinationRect.w = std::max(1, static_cast<int>(std::round(scaledWidth)));
    } else {
        sourceRect.x = static_cast<int>(std::floor(panX_));
        sourceRect.w = std::min(static_cast<int>(bounds.x + bounds.width) - sourceRect.x,
            std::max(1, static_cast<int>(std::ceil(screenWidth_ / zoom_))));
        destinationRect.x = 0;
        destinationRect.w = screenWidth_;
    }

    if (scaledHeight <= static_cast<float>(screenHeight_)) {
        sourceRect.y = static_cast<int>(bounds.y);
        sourceRect.h = static_cast<int>(bounds.height);
        destinationRect.y = static_cast<int>((screenHeight_ - scaledHeight) * 0.5f);
        destinationRect.h = std::max(1, static_cast<int>(std::round(scaledHeight)));
    } else {
        sourceRect.y = static_cast<int>(std::floor(panY_));
        sourceRect.h = std::min(static_cast<int>(bounds.y + bounds.height) - sourceRect.y,
            std::max(1, static_cast<int>(std::ceil(screenHeight_ / zoom_))));
        destinationRect.y = 0;
        destinationRect.h = screenHeight_;
    }

    if (SDL_BlitScaled(source_.get(), &sourceRect, viewport_.get(), &destinationRect) != 0) {
        lastError_ = std::string("Viewport scaling failed: ") + SDL_GetError();
        return;
    }
    if (SDL_UpdateTexture(viewportTexture_.get(), nullptr,
                          viewport_->pixels, viewport_->pitch) != 0) {
        lastError_ = std::string("Viewport upload failed: ") + SDL_GetError();
        return;
    }
    dirty_ = false;
}

void MangaReader::render() {
    if (!source_ || !isReady()) return;
    rebuildViewport();
    SDL_RenderCopy(renderer_, viewportTexture_.get(), nullptr, nullptr);
}
