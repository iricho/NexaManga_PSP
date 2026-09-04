#include "ThumbnailCache.hpp"

#include "JpegDecoder.hpp"
#include "Log.hpp"
#include "PageSource.hpp"
#include "PathUtils.hpp"
#include "PerformanceClock.hpp"

#include <SDL_image.h>
#include <algorithm>
#include <limits>
#include <utility>

namespace {

struct SurfaceDeleter {
    void operator()(SDL_Surface* surface) const { if (surface) SDL_FreeSurface(surface); }
};
using Surface = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

bool coverPage(const Series& series, PageData& page) {
    if (!series.coverPath.empty()) {
        page = {};
        page.filesystemPath = series.coverPath;
        page.logicalName = PathUtils::filename(series.coverPath);
        page.identifier = PathUtils::normalizedKey(series.coverPath);
        return true;
    }
    if (!series.hasPageCover()) return false;
    std::unique_ptr<PageSource> source = createPageSource(series.chapters[series.coverChapterIndex]);
    return source && source->available() && source->readPage(series.coverPageIndex, page);
}

Surface decodePreview(const PageData& page) {
    JpegDecoder decoder;
    if (decoder.matches(page)) {
        ImageDecodeRequest request;
        request.representation = ImageRepresentation::ScaledPreview;
        request.scaleDenominator = 8;
        ImageInfo info;
        std::string error;
        if (decoder.probe(page, request, info, error) &&
            (info.outputWidth < 72 || info.outputHeight < 96)) {
            request.scaleDenominator = 4;
        }
        if (decoder.probe(page, request, info, error) && info.outputWidth && info.outputHeight) {
            Surface surface(SDL_CreateRGBSurfaceWithFormat(
                0, static_cast<int>(info.outputWidth), static_cast<int>(info.outputHeight),
                16, SDL_PIXELFORMAT_RGB565));
            if (surface) {
                Rgb565Target target {surface->pixels, static_cast<std::size_t>(surface->pitch),
                                     info.outputWidth, info.outputHeight};
                if (decoder.decodeRgb565(page, request, target).success) return surface;
            }
        }
    }

    SDL_Surface* loaded = nullptr;
    if (!page.filesystemPath.empty()) {
        loaded = IMG_Load(page.filesystemPath.c_str());
    } else if (!page.bytes.empty() && page.bytes.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        SDL_RWops* rw = SDL_RWFromConstMem(page.bytes.data(), static_cast<int>(page.bytes.size()));
        if (rw) loaded = IMG_Load_RW(rw, 1);
    }
    return Surface(loaded);
}

} // namespace

ThumbnailCache::ThumbnailCache(SDL_Renderer* renderer, int width, int height,
                               std::size_t budgetBytes)
    : renderer_(renderer), width_(width), height_(height), budgetBytes_(budgetBytes) {
}

ThumbnailCache::~ThumbnailCache() { clear(); }

void ThumbnailCache::clear() {
    for (auto& item : entries_) if (item.second.texture) SDL_DestroyTexture(item.second.texture);
    entries_.clear();
    residentBytes_ = 0;
}

void ThumbnailCache::evictFor(std::size_t bytes) {
    while (!entries_.empty() && residentBytes_ + bytes > budgetBytes_) {
        auto oldest = std::min_element(entries_.begin(), entries_.end(),
            [](const auto& left, const auto& right) {
                return left.second.access < right.second.access;
            });
        residentBytes_ -= oldest->second.bytes;
        if (oldest->second.texture) SDL_DestroyTexture(oldest->second.texture);
        entries_.erase(oldest);
    }
}

SDL_Texture* ThumbnailCache::generate(const Series& series) {
    PageData page;
    if (!coverPage(series, page)) return nullptr;
    Surface source = decodePreview(page);
    if (!source) return nullptr;

    Surface thumbnail(SDL_CreateRGBSurfaceWithFormat(
        0, width_, height_, 16, SDL_PIXELFORMAT_RGB565));
    if (!thumbnail) return nullptr;
    SDL_FillRect(thumbnail.get(), nullptr, SDL_MapRGB(thumbnail->format, 34, 37, 43));

    const float scale = std::min(static_cast<float>(width_) / source->w,
                                 static_cast<float>(height_) / source->h);
    SDL_Rect destination {
        static_cast<int>((width_ - source->w * scale) * 0.5f),
        static_cast<int>((height_ - source->h * scale) * 0.5f),
        std::max(1, static_cast<int>(source->w * scale)),
        std::max(1, static_cast<int>(source->h * scale))
    };
    if (SDL_BlitScaled(source.get(), nullptr, thumbnail.get(), &destination) != 0) return nullptr;
    return SDL_CreateTextureFromSurface(renderer_, thumbnail.get());
}

SDL_Texture* ThumbnailCache::get(const Series& series) {
    const std::string key = PathUtils::normalizedKey(series.path);
    auto found = entries_.find(key);
    if (found != entries_.end()) {
        found->second.access = ++accessCounter_;
        return found->second.texture;
    }
    if (generatedThisFrame_) return nullptr;
    generatedThisFrame_ = true;
    ++generationAttempts_;
    const std::uint64_t started = PerformanceClock::nowMicros();
    SDL_Texture* texture = generate(series);
    lastGenerationMicros_ = PerformanceClock::elapsedMicros(started);
    peakGenerationMicros_ = std::max(peakGenerationMicros_, lastGenerationMicros_);
    if (!texture) {
        Log::warning("Thumbnail generation failed for " + series.path +
                     "; using placeholder cover.");
        entries_.emplace(key, Entry {nullptr, 0, ++accessCounter_});
        return nullptr;
    }
    ++generationSuccesses_;
    const std::size_t bytes = static_cast<std::size_t>(width_) * height_ * 2u;
    evictFor(bytes);
    entries_.emplace(key, Entry {texture, bytes, ++accessCounter_});
    residentBytes_ += bytes;
    return texture;
}
