#pragma once

#include "Model.hpp"

#include <SDL.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

class ThumbnailCache {
public:
    ThumbnailCache(SDL_Renderer* renderer, int width = 72, int height = 96,
                   std::size_t budgetBytes = 2u * 1024u * 1024u);
    ~ThumbnailCache();

    ThumbnailCache(const ThumbnailCache&) = delete;
    ThumbnailCache& operator=(const ThumbnailCache&) = delete;

    SDL_Texture* get(const Series& series);
    void beginFrame() { generatedThisFrame_ = false; }
    void clear();
    std::size_t residentBytes() const { return residentBytes_; }
    std::size_t entryCount() const { return entries_.size(); }
    std::size_t generationAttempts() const { return generationAttempts_; }
    std::size_t generationSuccesses() const { return generationSuccesses_; }
    std::uint64_t lastGenerationMicros() const { return lastGenerationMicros_; }
    std::uint64_t peakGenerationMicros() const { return peakGenerationMicros_; }

private:
    struct Entry {
        SDL_Texture* texture = nullptr;
        std::size_t bytes = 0;
        std::uint64_t access = 0;
    };

    SDL_Texture* generate(const Series& series);
    void evictFor(std::size_t bytes);

    SDL_Renderer* renderer_ = nullptr;
    int width_ = 72;
    int height_ = 96;
    std::size_t budgetBytes_ = 0;
    std::size_t residentBytes_ = 0;
    std::uint64_t accessCounter_ = 0;
    bool generatedThisFrame_ = false;
    std::size_t generationAttempts_ = 0;
    std::size_t generationSuccesses_ = 0;
    std::uint64_t lastGenerationMicros_ = 0;
    std::uint64_t peakGenerationMicros_ = 0;
    std::unordered_map<std::string, Entry> entries_;
};
