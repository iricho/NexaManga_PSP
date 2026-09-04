#include "ImageLoader.hpp"

#include "ImageProbe.hpp"
#include "JpegDecoder.hpp"
#include "Log.hpp"
#include "MemoryMath.hpp"
#include "PerformanceClock.hpp"

#include <SDL_image.h>

#include <algorithm>
#include <climits>
#include <limits>
#include <sstream>

namespace {

std::string budgetMessage(std::size_t peak, std::size_t budget) {
    std::ostringstream message;
    message << "Image exceeds memory budget (predicted peak "
            << peak / 1024u << " KiB, budget " << budget / 1024u << " KiB).";
    return message.str();
}

} // namespace

ImageLoader::ImageLoader(std::size_t memoryBudgetBytes)
    : memoryBudgetBytes_(memoryBudgetBytes) {
}

std::size_t ImageLoader::bytesFor(const SDL_Surface* surface) {
    if (!surface || surface->pitch <= 0 || surface->h <= 0) return 0;
    return MemoryMath::saturatedMultiply(static_cast<std::size_t>(surface->pitch),
                                          static_cast<std::size_t>(surface->h));
}

void ImageLoader::recordResult(const ImageLoadResult& result) const {
#if MANGAPSP_ENABLE_METRICS
    metrics_.totalSourceReadMicros += result.timing.sourceReadMicros;
    metrics_.totalProbeMicros += result.timing.headerProbeMicros;
    metrics_.totalDecodeMicros += result.timing.jpegDecodeMicros +
                                  result.timing.fallbackDecodeMicros +
                                  result.timing.rgb565OutputMicros;
    metrics_.peakDecodeMicros = std::max(
        metrics_.peakDecodeMicros,
        result.timing.jpegDecodeMicros + result.timing.fallbackDecodeMicros +
        result.timing.rgb565OutputMicros);
    metrics_.peakPredictedBytes = std::max(metrics_.peakPredictedBytes,
                                           result.estimatedPeakBytes);
    if (!result.surface) {
        ++metrics_.failedLoads;
        if (result.info.format == ImageFormat::Jpeg) ++metrics_.jpegDecodeFailures;
        if (result.failure == ImageLoadFailure::Budget) ++metrics_.budgetRejections;
        return;
    }
    ++metrics_.successfulLoads;
    if (result.info.format == ImageFormat::Jpeg) ++metrics_.directJpegDecodes;
    if (result.info.format == ImageFormat::Png) ++metrics_.pngFallbackDecodes;
    metrics_.decodedPixels = MemoryMath::saturatedAdd(
        metrics_.decodedPixels,
        MemoryMath::saturatedMultiply(result.info.outputWidth, result.info.outputHeight));
    metrics_.finalRgb565Bytes = MemoryMath::saturatedAdd(
        metrics_.finalRgb565Bytes, result.surfaceBytes);
#else
    (void)result;
#endif
}

ImageLoadResult ImageLoader::loadRgb565(PageSource& source, std::size_t pageIndex,
                                        std::size_t retainedBytes) const {
    ImageLoadResult result;
    const std::uint64_t totalStarted = PerformanceClock::nowMicros();
    PageData page;
    const std::uint64_t sourceStarted = PerformanceClock::nowMicros();
    if (!source.readPage(pageIndex, page)) {
        result.timing.sourceReadMicros = PerformanceClock::elapsedMicros(sourceStarted);
        result.timing.totalMicros = PerformanceClock::elapsedMicros(totalStarted);
        result.failure = ImageLoadFailure::Source;
        result.error = source.lastError();
        Log::error(source.sourcePath() + ": " + result.error);
        recordResult(result);
        return result;
    }
    result.timing.sourceReadMicros = PerformanceClock::elapsedMicros(sourceStarted);
    result.encodedBytes = page.bytes.size();

    ImageDecodeRequest request;
    if (!ImageProbe::probe(page, request, result.info, result.error,
                           &result.timing.headerProbeMicros)) {
        result.failure = ImageLoadFailure::Probe;
        result.timing.totalMicros = PerformanceClock::elapsedMicros(totalStarted);
        Log::error(page.identifier + ": image probe failed: " + result.error);
        recordResult(result);
        return result;
    }

    result.estimatedLibraryOverheadBytes = result.info.estimatedLibraryOverheadBytes;
    result.trackedPeakBytes = MemoryMath::saturatedAdd(
        retainedBytes, result.info.trackedTransientBytes);
    result.estimatedPeakBytes = MemoryMath::saturatedAdd(
        retainedBytes, result.info.predictedTransientBytes);
    if (memoryBudgetBytes_ > 0 && result.estimatedPeakBytes > memoryBudgetBytes_) {
        result.failure = ImageLoadFailure::Budget;
        result.error = budgetMessage(result.estimatedPeakBytes, memoryBudgetBytes_);
        result.timing.totalMicros = PerformanceClock::elapsedMicros(totalStarted);
        Log::warning(page.identifier + ": " + result.error);
        recordResult(result);
        return result;
    }

    if (result.info.outputWidth > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        result.info.outputHeight > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        result.failure = ImageLoadFailure::InvalidDimensions;
        result.error = "Image dimensions exceed SDL limits.";
        result.timing.totalMicros = PerformanceClock::elapsedMicros(totalStarted);
        recordResult(result);
        return result;
    }

    if (result.info.format == ImageFormat::Jpeg) {
        SurfacePtr target(SDL_CreateRGBSurfaceWithFormat(
            0, static_cast<int>(result.info.outputWidth),
            static_cast<int>(result.info.outputHeight), 16, SDL_PIXELFORMAT_RGB565));
        if (!target) {
            result.failure = ImageLoadFailure::Conversion;
            result.error = std::string("RGB565 allocation failed: ") + SDL_GetError();
            result.timing.totalMicros = PerformanceClock::elapsedMicros(totalStarted);
            recordResult(result);
            return result;
        }

        result.surfaceBytes = bytesFor(target.get());
        result.info.finalRgb565Bytes = result.surfaceBytes;
        result.trackedPeakBytes = MemoryMath::saturatedAdd(
            retainedBytes, MemoryMath::saturatedAdd(
                result.encodedBytes, MemoryMath::saturatedAdd(
                    result.surfaceBytes, result.info.scanlineWorkspaceBytes)));
        result.estimatedPeakBytes = MemoryMath::saturatedAdd(
            result.trackedPeakBytes, result.estimatedLibraryOverheadBytes);
        if (memoryBudgetBytes_ > 0 && result.estimatedPeakBytes > memoryBudgetBytes_) {
            result.failure = ImageLoadFailure::Budget;
            result.error = budgetMessage(result.estimatedPeakBytes, memoryBudgetBytes_);
            result.timing.totalMicros = PerformanceClock::elapsedMicros(totalStarted);
            Log::warning(page.identifier + ": " + result.error);
            recordResult(result);
            return result;
        }

        const std::size_t committed = MemoryMath::saturatedAdd(
            retainedBytes, MemoryMath::saturatedAdd(
                result.encodedBytes, MemoryMath::saturatedAdd(
                    result.surfaceBytes, result.info.scanlineWorkspaceBytes)));
        request.maxLibraryMemoryBytes = memoryBudgetBytes_ > committed
            ? memoryBudgetBytes_ - committed : 0;
        JpegDecoder decoder;
        ImageDecodeResult decoded = decoder.decodeRgb565(page, request, {
            target->pixels, static_cast<std::size_t>(target->pitch),
            result.info.outputWidth, result.info.outputHeight
        });
        result.timing.headerProbeMicros += decoded.timing.headerProbeMicros;
        result.timing.jpegDecodeMicros = decoded.timing.jpegDecodeMicros;
        result.timing.rgb565OutputMicros = decoded.timing.rgb565OutputMicros;
        if (!decoded.success) {
            result.failure = ImageLoadFailure::Decode;
            result.error = "JPEG decode failed: " + decoded.error;
            result.timing.totalMicros = PerformanceClock::elapsedMicros(totalStarted);
            Log::error(page.identifier + ": " + result.error);
            recordResult(result);
            return result;
        }
        result.surface = SharedSurface(std::move(target));
    } else {
        const std::uint64_t decodeStarted = PerformanceClock::nowMicros();
        SurfacePtr decoded;
        if (!page.filesystemPath.empty()) {
            decoded.reset(IMG_Load(page.filesystemPath.c_str()));
        } else if (!page.bytes.empty() &&
                   page.bytes.size() <= static_cast<std::size_t>(INT_MAX)) {
            SDL_RWops* stream = SDL_RWFromConstMem(
                page.bytes.data(), static_cast<int>(page.bytes.size()));
            if (stream) decoded.reset(IMG_Load_RW(stream, 1));
        }
        result.timing.fallbackDecodeMicros = PerformanceClock::elapsedMicros(decodeStarted);
        if (!decoded) {
            result.failure = ImageLoadFailure::Decode;
            result.error = std::string("PNG fallback decode failed: ") + IMG_GetError();
            result.timing.totalMicros = PerformanceClock::elapsedMicros(totalStarted);
            Log::error(page.identifier + ": " + result.error);
            recordResult(result);
            return result;
        }
        if (decoded->w <= 0 || decoded->h <= 0 ||
            static_cast<std::uint32_t>(decoded->w) != result.info.outputWidth ||
            static_cast<std::uint32_t>(decoded->h) != result.info.outputHeight) {
            result.failure = ImageLoadFailure::InvalidDimensions;
            result.error = "PNG decoded dimensions do not match its IHDR.";
            result.timing.totalMicros = PerformanceClock::elapsedMicros(totalStarted);
            recordResult(result);
            return result;
        }

        result.decodedBytes = bytesFor(decoded.get());
        result.trackedPeakBytes = MemoryMath::saturatedAdd(
            retainedBytes, MemoryMath::saturatedAdd(result.encodedBytes,
                MemoryMath::saturatedAdd(result.decodedBytes, result.info.finalRgb565Bytes)));
        result.estimatedPeakBytes = MemoryMath::saturatedAdd(
            result.trackedPeakBytes, result.estimatedLibraryOverheadBytes);
        if (memoryBudgetBytes_ > 0 && result.estimatedPeakBytes > memoryBudgetBytes_) {
            result.failure = ImageLoadFailure::Budget;
            result.error = budgetMessage(result.estimatedPeakBytes, memoryBudgetBytes_);
            result.timing.totalMicros = PerformanceClock::elapsedMicros(totalStarted);
            Log::warning(page.identifier + ": " + result.error);
            recordResult(result);
            return result;
        }

        const std::uint64_t outputStarted = PerformanceClock::nowMicros();
        SurfacePtr converted(SDL_ConvertSurfaceFormat(
            decoded.get(), SDL_PIXELFORMAT_RGB565, 0));
        result.timing.rgb565OutputMicros = PerformanceClock::elapsedMicros(outputStarted);
        if (!converted) {
            result.failure = ImageLoadFailure::Conversion;
            result.error = std::string("PNG RGB565 conversion failed: ") + SDL_GetError();
            result.timing.totalMicros = PerformanceClock::elapsedMicros(totalStarted);
            Log::error(page.identifier + ": " + result.error);
            recordResult(result);
            return result;
        }
        result.surfaceBytes = bytesFor(converted.get());
        result.surface = SharedSurface(std::move(converted));
    }

    result.timing.totalMicros = PerformanceClock::elapsedMicros(totalStarted);
#if MANGAPSP_DEVELOPMENT
    std::ostringstream loaded;
    loaded << "Loaded " << page.identifier << " (" << result.surface->w << "x"
           << result.surface->h << ", " << imageFormatName(result.info.format)
           << " -> RGB565 " << result.surfaceBytes / 1024u << " KiB, predicted peak "
           << result.estimatedPeakBytes / 1024u << " KiB, "
           << result.timing.totalMicros / 1000u << " ms)";
    MANGAPSP_LOG_INFO(loaded.str());
#endif
    recordResult(result);
    return result;
}
