#pragma once

#include <cstddef>

struct LibraryScreenState {
    std::size_t selectedSeries = 0;
    bool continueFocused = true;
    std::size_t sortMode = 0;
};

struct SeriesScreenState {
    std::size_t selectedChapter = 0;
    std::size_t firstVisibleChapter = 0;
};

struct ReaderOverlayState {
    bool menuOpen = false;
    std::size_t menuSelection = 0;
};

struct BookmarkScreenState {
    std::size_t selection = 0;
    bool openedFromReader = false;
};

struct MenuScreenState {
    std::size_t selection = 0;
};
