#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

enum class ReadingDirection {
    RightToLeft,
    LeftToRight
};

enum class SpreadMode {
    Auto,
    FullSpread,
    SplitSpread
};

struct SourceBounds {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct ReadingStop {
    float panX = 0.0f;
    float panY = 0.0f;
    std::uint8_t spreadHalf = 0;
};

struct LogicalPagePosition {
    std::size_t physicalPage = 0;
    std::size_t logicalStop = 0;
};

namespace ReaderExperience {

bool isLikelySpread(std::uint32_t width, std::uint32_t height);
bool usesSplitSpread(SpreadMode mode, std::uint32_t width, std::uint32_t height);

std::vector<SourceBounds> spreadBounds(std::uint32_t width, std::uint32_t height,
                                       ReadingDirection direction);

std::vector<ReadingStop> buildStops(std::uint32_t pageWidth,
                                    std::uint32_t pageHeight,
                                    std::uint32_t viewportWidth,
                                    std::uint32_t viewportHeight,
                                    float zoom,
                                    ReadingDirection direction,
                                    SpreadMode spreadMode,
                                    float overlapFraction = 0.15f);

std::size_t nearestStop(const std::vector<ReadingStop>& stops,
                        float panX, float panY, std::uint8_t spreadHalf = 0);

bool shouldMarkChapterCompleted(std::size_t pageIndex,
                                std::size_t pageCount,
                                bool reachedByForwardNavigation);

bool advanceLogicalPosition(LogicalPagePosition& position,
                            std::size_t pageCount,
                            std::size_t stopsOnCurrentPage);
bool retreatLogicalPosition(LogicalPagePosition& position,
                            std::size_t stopsOnPreviousPage);

} // namespace ReaderExperience
