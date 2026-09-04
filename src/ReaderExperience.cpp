#include "ReaderExperience.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

std::vector<float> axisStops(float origin, float extent, float visible,
                             float overlapFraction) {
    std::vector<float> result;
    const float travel = std::max(0.0f, extent - visible);
    if (travel <= 0.5f) {
        result.push_back(origin);
        return result;
    }

    const float overlap = std::max(0.0f, std::min(0.45f, overlapFraction));
    const float step = std::max(1.0f, visible * (1.0f - overlap));
    for (float offset = 0.0f; offset < travel; offset += step) {
        result.push_back(origin + offset);
    }
    const float end = origin + travel;
    if (result.empty() || std::abs(result.back() - end) > 0.5f) result.push_back(end);
    return result;
}

void appendBoundsStops(std::vector<ReadingStop>& output,
                       const SourceBounds& bounds,
                       std::uint8_t half,
                       std::uint32_t viewportWidth,
                       std::uint32_t viewportHeight,
                       float zoom,
                       ReadingDirection direction,
                       float overlapFraction) {
    const float safeZoom = std::max(0.0001f, zoom);
    const float visibleWidth = static_cast<float>(viewportWidth) / safeZoom;
    const float visibleHeight = static_cast<float>(viewportHeight) / safeZoom;
    std::vector<float> columns = axisStops(
        bounds.x, bounds.width, visibleWidth, overlapFraction);
    const std::vector<float> rows = axisStops(
        bounds.y, bounds.height, visibleHeight, overlapFraction);
    if (direction == ReadingDirection::RightToLeft) {
        std::reverse(columns.begin(), columns.end());
    }
    for (const float x : columns) {
        for (const float y : rows) output.push_back({x, y, half});
    }
}

} // namespace

namespace ReaderExperience {

bool isLikelySpread(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0 || width <= height) return false;
    const float ratio = static_cast<float>(width) / static_cast<float>(height);
    const float halfRatio = static_cast<float>(width) * 0.5f /
                            static_cast<float>(height);
    // Require both a clearly landscape page and two plausible portrait-ish halves.
    return ratio >= 1.35f && ratio <= 2.8f && halfRatio >= 0.55f && halfRatio <= 1.4f;
}

bool usesSplitSpread(SpreadMode mode, std::uint32_t width, std::uint32_t height) {
    if (mode == SpreadMode::FullSpread) return false;
    if (mode == SpreadMode::SplitSpread) return width > 1 && height > 0;
    return isLikelySpread(width, height);
}

std::vector<SourceBounds> spreadBounds(std::uint32_t width, std::uint32_t height,
                                       ReadingDirection direction) {
    const std::uint32_t leftWidth = width / 2u;
    const std::uint32_t rightWidth = width - leftWidth;
    const SourceBounds left {0.0f, 0.0f, static_cast<float>(leftWidth),
                             static_cast<float>(height)};
    const SourceBounds right {static_cast<float>(leftWidth), 0.0f,
                              static_cast<float>(rightWidth),
                              static_cast<float>(height)};
    return direction == ReadingDirection::RightToLeft
        ? std::vector<SourceBounds> {right, left}
        : std::vector<SourceBounds> {left, right};
}

std::vector<ReadingStop> buildStops(std::uint32_t pageWidth,
                                    std::uint32_t pageHeight,
                                    std::uint32_t viewportWidth,
                                    std::uint32_t viewportHeight,
                                    float zoom,
                                    ReadingDirection direction,
                                    SpreadMode spreadMode,
                                    float overlapFraction) {
    std::vector<ReadingStop> result;
    if (pageWidth == 0 || pageHeight == 0 || viewportWidth == 0 ||
        viewportHeight == 0 || zoom <= 0.0f) return result;

    if (usesSplitSpread(spreadMode, pageWidth, pageHeight)) {
        const std::vector<SourceBounds> halves = spreadBounds(pageWidth, pageHeight, direction);
        for (std::size_t index = 0; index < halves.size(); ++index) {
            appendBoundsStops(result, halves[index], static_cast<std::uint8_t>(index),
                              viewportWidth, viewportHeight, zoom, direction,
                              overlapFraction);
        }
    } else {
        appendBoundsStops(result,
            {0.0f, 0.0f, static_cast<float>(pageWidth), static_cast<float>(pageHeight)},
            0, viewportWidth, viewportHeight, zoom, direction, overlapFraction);
    }
    return result;
}

std::size_t nearestStop(const std::vector<ReadingStop>& stops,
                        float panX, float panY, std::uint8_t spreadHalf) {
    if (stops.empty()) return 0;
    std::size_t best = 0;
    float bestDistance = std::numeric_limits<float>::max();
    for (std::size_t index = 0; index < stops.size(); ++index) {
        const float halfPenalty = stops[index].spreadHalf == spreadHalf ? 0.0f : 1000000.0f;
        const float dx = stops[index].panX - panX;
        const float dy = stops[index].panY - panY;
        const float distance = dx * dx + dy * dy + halfPenalty;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = index;
        }
    }
    return best;
}

bool shouldMarkChapterCompleted(std::size_t pageIndex,
                                std::size_t pageCount,
                                bool reachedByForwardNavigation) {
    return reachedByForwardNavigation && pageCount > 0 && pageIndex + 1u >= pageCount;
}

bool advanceLogicalPosition(LogicalPagePosition& position,
                            std::size_t pageCount,
                            std::size_t stopsOnCurrentPage) {
    const std::size_t safeStops = std::max<std::size_t>(1u, stopsOnCurrentPage);
    if (position.logicalStop + 1u < safeStops) {
        ++position.logicalStop;
        return true;
    }
    if (position.physicalPage + 1u >= pageCount) return false;
    ++position.physicalPage;
    position.logicalStop = 0;
    return true;
}

bool retreatLogicalPosition(LogicalPagePosition& position,
                            std::size_t stopsOnPreviousPage) {
    if (position.logicalStop > 0) {
        --position.logicalStop;
        return true;
    }
    if (position.physicalPage == 0) return false;
    --position.physicalPage;
    position.logicalStop = std::max<std::size_t>(1u, stopsOnPreviousPage) - 1u;
    return true;
}

} // namespace ReaderExperience
