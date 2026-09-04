#include "ReaderExperience.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void expect(bool condition, const std::string& message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
}

int main() {
    const auto ltr = ReaderExperience::buildStops(
        1000, 1400, 480, 272, 1.0f, ReadingDirection::LeftToRight, SpreadMode::FullSpread);
    const auto rtl = ReaderExperience::buildStops(
        1000, 1400, 480, 272, 1.0f, ReadingDirection::RightToLeft, SpreadMode::FullSpread);
    expect(!ltr.empty() && ltr.front().panX == 0.0f, "LTR begins at left column");
    expect(!rtl.empty() && rtl.front().panX > rtl.back().panX, "RTL begins at right column");
    expect(ltr.size() == rtl.size(), "direction changes ordering, not coverage");

    const auto tall = ReaderExperience::buildStops(
        480, 1800, 480, 272, 1.0f, ReadingDirection::RightToLeft, SpreadMode::FullSpread);
    const auto shortPage = ReaderExperience::buildStops(
        480, 200, 480, 272, 1.0f, ReadingDirection::RightToLeft, SpreadMode::FullSpread);
    expect(tall.size() > 2, "tall page creates vertical stops");
    expect(shortPage.size() == 1, "short page creates one stop");
    if (tall.size() > 1) {
        const float step = tall[1].panY - tall[0].panY;
        expect(step > 0.0f && step < 272.0f, "smart stops retain vertical overlap");
    }

    expect(ReaderExperience::isLikelySpread(1600, 1000), "tolerant wide spread detection");
    expect(!ReaderExperience::isLikelySpread(1100, 1000), "near-square page is not spread");
    expect(!ReaderExperience::isLikelySpread(4000, 1000), "extreme panorama is not false spread");
    const auto rtlHalves = ReaderExperience::spreadBounds(1601, 1000, ReadingDirection::RightToLeft);
    const auto ltrHalves = ReaderExperience::spreadBounds(1601, 1000, ReadingDirection::LeftToRight);
    expect(rtlHalves[0].x > rtlHalves[1].x, "RTL spread reads right half first");
    expect(ltrHalves[0].x < ltrHalves[1].x, "LTR spread reads left half first");

    const auto split = ReaderExperience::buildStops(
        1600, 1000, 480, 272, 0.6f, ReadingDirection::RightToLeft, SpreadMode::SplitSpread);
    bool sawSecondHalf = false;
    for (const ReadingStop& stop : split) if (stop.spreadHalf == 1) sawSecondHalf = true;
    expect(sawSecondHalf, "split spread creates two logical halves");
    LogicalPagePosition position {3, 0};
    ReaderExperience::advanceLogicalPosition(position, 8, split.size());
    expect(position.physicalPage == 3 && position.logicalStop == 1,
           "split progression advances logically without changing physical page");

    LogicalPagePosition traversal {0, 0};
    for (std::size_t i = 1; i < tall.size(); ++i) {
        ReaderExperience::advanceLogicalPosition(traversal, 2, tall.size());
    }
    expect(traversal.physicalPage == 0 && traversal.logicalStop + 1 == tall.size(),
           "forward smart traversal reaches final stop");
    ReaderExperience::advanceLogicalPosition(traversal, 2, tall.size());
    expect(traversal.physicalPage == 1 && traversal.logicalStop == 0,
           "forward smart traversal advances physical page after final stop");
    ReaderExperience::retreatLogicalPosition(traversal, tall.size());
    expect(traversal.physicalPage == 0 && traversal.logicalStop + 1 == tall.size(),
           "reverse smart traversal returns to previous page final stop");
    expect(ReaderExperience::nearestStop(tall, tall.back().panX, tall.back().panY) == tall.size() - 1,
           "manual pan resynchronizes to nearest stop");

    expect(!ReaderExperience::shouldMarkChapterCompleted(9, 10, false),
           "opening final page does not complete chapter");
    expect(ReaderExperience::shouldMarkChapterCompleted(9, 10, true),
           "forward navigation reaching final page completes chapter");
    expect(!ReaderExperience::shouldMarkChapterCompleted(8, 10, true),
           "non-final navigation does not complete chapter");

    if (failures) return 1;
    std::cout << "All reader experience tests passed\n";
    return 0;
}
