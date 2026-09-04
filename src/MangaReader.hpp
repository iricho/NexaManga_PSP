#pragma once

#include "ImageLoader.hpp"
#include "Model.hpp"
#include "PageCache.hpp"
#include "PageSource.hpp"
#include "ReaderExperience.hpp"

#include <SDL.h>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

struct TextureDeleter {
    void operator()(SDL_Texture* texture) const {
        if (texture) SDL_DestroyTexture(texture);
    }
};

using TexturePtr = std::unique_ptr<SDL_Texture, TextureDeleter>;

class MangaReader {
public:
    enum class FitMode {
        Page,
        Width
    };

    MangaReader(SDL_Renderer* renderer, int screenWidth, int screenHeight,
                std::size_t memoryBudgetBytes);

    bool isReady() const;
    bool openChapter(const Chapter* chapter, std::size_t startPage = 0);
    void close();

    bool nextPage();
    bool previousPage();
    bool advanceReading();
    bool retreatReading();
    bool preloadOne();
    void prepareForSuspend();
    bool resumeAfterSuspend();

    void zoomIn();
    void smartZoomIn();
    void zoomOut();
    void toggleFitMode();
    void setFitMode(FitMode mode);
    void resetView();
    void invalidateViewport() { dirty_ = true; }
    void pan(float screenDx, float screenDy);
    void setReadingDirection(ReadingDirection direction);
    void setSmartReading(bool enabled);
    void setSpreadMode(SpreadMode mode);
    bool restoreLogicalPosition(std::size_t position);

    void render();

    std::size_t currentPageIndex() const { return pageIndex_; }
    std::size_t pageCount() const;
    FitMode fitMode() const { return fitMode_; }
    float zoom() const { return zoom_; }
    ReadingDirection readingDirection() const { return direction_; }
    bool smartReading() const { return smartReading_; }
    SpreadMode spreadMode() const { return spreadMode_; }
    bool splitSpreadActive() const;
    bool likelySpread() const;
    std::size_t logicalPosition() const { return logicalIndex_; }
    std::size_t logicalPositionCount() const { return logicalStops_.size(); }
    bool completionReached() const { return completionReached_; }
    int sourceWidth() const { return source_ ? source_->w : 0; }
    int sourceHeight() const { return source_ ? source_->h : 0; }
    std::size_t imageMemoryBytes() const { return sourceMemoryBytes_; }
    std::size_t totalTrackedMemoryBytes() const;
    std::size_t lastDecodePeakBytes() const { return lastDecodePeakBytes_; }
    std::size_t lastTrackedDecodePeakBytes() const { return lastTrackedDecodePeakBytes_; }
    std::size_t lastDecoderEstimateBytes() const { return lastDecoderEstimateBytes_; }
    std::size_t lastCompressedBytes() const { return lastCompressedBytes_; }
    std::size_t lastScanlineBytes() const { return lastScanlineBytes_; }
    const DecodeTiming& lastDecodeTiming() const { return lastDecodeTiming_; }
    const ImageLoaderMetrics& imageLoaderMetrics() const { return imageLoader_.metrics(); }
    std::size_t memoryBudgetBytes() const { return imageLoader_.memoryBudgetBytes(); }
    const PageCacheStats& cacheStats() const { return cache_.stats(); }
    std::size_t cachedPageCount() const { return cache_.entryCount(); }
    std::size_t cachedImageBytes() const {
        return cache_.residentBytes() >= sourceMemoryBytes_
            ? cache_.residentBytes() - sourceMemoryBytes_ : 0;
    }
    std::size_t viewportTextureMemoryBytes() const { return fixedMemoryBytes(); }
    std::size_t previousPageMemoryBytes() const {
        return pageIndex_ > 0 ? cache_.bytesForIndex(pageIndex_ - 1) : 0;
    }
    std::size_t nextPageMemoryBytes() const {
        return pageIndex_ + 1 < pageCount() ? cache_.bytesForIndex(pageIndex_ + 1) : 0;
    }
    bool preloadInProgress() const { return preloadInProgress_; }
    int preloadStage() const { return preloadStage_; }
    std::size_t preloadAttempts() const { return preloadAttempts_; }
    std::size_t preloadCompletions() const { return preloadCompletions_; }
    bool suspended() const { return suspended_; }

    const std::string& lastError() const { return lastError_; }

private:
    bool loadPage(std::size_t index, bool preload = false);
    bool navigate(int direction);
    void recordLoadDiagnostics(const ImageLoadResult& loaded);
    void releasePage();
    std::size_t fixedMemoryBytes() const;
    void rebuildViewport();
    void clampPan();
    void rebuildLogicalStops(std::size_t preferred = 0);
    void applyLogicalStop(std::size_t index);
    SourceBounds activeBounds() const;

    SDL_Renderer* renderer_ = nullptr;
    int screenWidth_ = 480;
    int screenHeight_ = 272;

    const Chapter* chapter_ = nullptr;
    std::unique_ptr<PageSource> pageSource_;
    std::size_t pageIndex_ = 0;

    ImageLoader imageLoader_;
    PageCache<SDL_Surface> cache_;
    SharedSurface source_;
    SurfacePtr viewport_;
    TexturePtr viewportTexture_;
    std::size_t sourceMemoryBytes_ = 0;
    std::size_t lastDecodePeakBytes_ = 0;
    std::size_t lastTrackedDecodePeakBytes_ = 0;
    std::size_t lastDecoderEstimateBytes_ = 0;
    std::size_t lastCompressedBytes_ = 0;
    std::size_t lastScanlineBytes_ = 0;
    DecodeTiming lastDecodeTiming_;
    std::unordered_set<std::size_t> failedPages_;
    ImageLoadFailure lastLoadFailure_ = ImageLoadFailure::None;
    int preloadStage_ = 0;
    std::size_t preloadAttempts_ = 0;
    std::size_t preloadCompletions_ = 0;
    bool preloadInProgress_ = false;
    bool suspended_ = false;

    FitMode fitMode_ = FitMode::Page;
    float zoom_ = 1.0f;
    float minZoom_ = 1.0f;
    float maxZoom_ = 2.0f;
    float panX_ = 0.0f;
    float panY_ = 0.0f;
    ReadingDirection direction_ = ReadingDirection::RightToLeft;
    SpreadMode spreadMode_ = SpreadMode::Auto;
    bool smartReading_ = true;
    std::vector<ReadingStop> logicalStops_;
    std::size_t logicalIndex_ = 0;
    bool manualPanNeedsResync_ = false;
    bool completionReached_ = false;

    bool dirty_ = true;
    std::string lastError_;
};
