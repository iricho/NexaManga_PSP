#pragma once

#include "ImageDecoder.hpp"
#include "PageSource.hpp"

#include <SDL.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

struct SurfaceDeleter {
    void operator()(SDL_Surface* surface) const {
        if (surface) SDL_FreeSurface(surface);
    }
};

using SurfacePtr = std::unique_ptr<SDL_Surface, SurfaceDeleter>;
using SharedSurface = std::shared_ptr<SDL_Surface>;

enum class ImageLoadFailure {
    None,
    Source,
    Probe,
    Decode,
    InvalidDimensions,
    Budget,
    Conversion
};

struct ImageLoaderMetrics {
    std::size_t successfulLoads = 0;
    std::size_t directJpegDecodes = 0;
    std::size_t pngFallbackDecodes = 0;
    std::size_t jpegDecodeFailures = 0;
    std::size_t failedLoads = 0;
    std::size_t budgetRejections = 0;
    std::size_t decodedPixels = 0;
    std::size_t finalRgb565Bytes = 0;
    std::size_t peakPredictedBytes = 0;
    std::uint64_t totalSourceReadMicros = 0;
    std::uint64_t totalProbeMicros = 0;
    std::uint64_t totalDecodeMicros = 0;
    std::uint64_t peakDecodeMicros = 0;
};

struct ImageLoadResult {
    SharedSurface surface;
    std::string error;
    ImageLoadFailure failure = ImageLoadFailure::None;
    ImageInfo info;
    DecodeTiming timing;
    std::size_t encodedBytes = 0;
    std::size_t decodedBytes = 0;
    std::size_t surfaceBytes = 0;
    std::size_t trackedPeakBytes = 0;
    std::size_t estimatedLibraryOverheadBytes = 0;
    std::size_t estimatedPeakBytes = 0;
};

class ImageLoader {
public:
    explicit ImageLoader(std::size_t memoryBudgetBytes);

    ImageLoadResult loadRgb565(PageSource& source, std::size_t pageIndex,
                               std::size_t retainedBytes = 0) const;

    std::size_t memoryBudgetBytes() const { return memoryBudgetBytes_; }
    const ImageLoaderMetrics& metrics() const { return metrics_; }
    static std::size_t bytesFor(const SDL_Surface* surface);

private:
    void recordResult(const ImageLoadResult& result) const;

    std::size_t memoryBudgetBytes_ = 0;
    mutable ImageLoaderMetrics metrics_;
};
