#pragma once

#include "Input.hpp"
#include "LibraryManager.hpp"
#include "MangaReader.hpp"
#include "SaveManager.hpp"
#include "SettingsManager.hpp"
#include "ThumbnailCache.hpp"
#include "UiState.hpp"
#include "UiTheme.hpp"

#include <SDL.h>
#include <cstdint>
#include <memory>
#include <limits>
#include <string>
#include <vector>

class App {
public:
    App();
    ~App();

    bool init();
    int run();
    const std::string& lastError() const { return lastError_; }

private:
    enum class Screen {
        Library,
        Series,
        Reader,
        Bookmarks,
        Options,
        Theme,
        About
    };

    void handleInput(const InputFrame& input);
    void handleLibraryInput(const InputFrame& input);
    void handleChapterInput(const InputFrame& input);
    void handleReaderInput(const InputFrame& input);
    void handleBookmarkInput(const InputFrame& input);
    void handleOptionsInput(const InputFrame& input);
    void handleThemeInput(const InputFrame& input);
    void handleAboutInput(const InputFrame& input);
    void handleLifecycle();

    void render();
    void renderLibrary();
    void renderChapters();
    void renderReader();
    void renderBookmarks();
    void renderOptions();
    void renderThemePicker();
    void renderAbout();
    void renderReaderMenu();
    void renderHardwareTestOverlay();
    void renderHeader(const std::string& title, const std::string& subtitle = std::string());
    void renderFooter(const std::string& text);
    void renderListItem(int y, const std::string& text, bool selected);
    void renderCenteredMessage(const std::string& line1, const std::string& line2 = std::string());
    void drawPanel(const SDL_Rect& rect, const UiColor& color);
    void drawSelectionBorder(const SDL_Rect& rect);
    void drawProgressBar(const SDL_Rect& rect, float progress);
    void drawCoverCard(const Series& series, const SDL_Rect& cover, bool selected);

    void refreshLibrary();
    bool openSelectedChapter(bool resume,
        std::size_t requestedPage = std::numeric_limits<std::size_t>::max());
    bool continueMostRecent();
    bool openBookmark(std::size_t index);
    void addCurrentBookmark();
    void rebuildLibraryOrder();
    void openOptions(Screen returnScreen);
    void openThemePicker(Screen returnScreen);
    void persistSettings();
    void activateReaderMenuItem();
    void saveCurrentProgress();
    static std::string truncate(const std::string& value, std::size_t maxChars);
    const UiTheme& theme() const;

    static constexpr int ScreenWidth = 480;
    static constexpr int ScreenHeight = 272;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    Input input_;
    std::unique_ptr<MangaReader> reader_;
    std::unique_ptr<ThumbnailCache> thumbnailCache_;
    LibraryManager libraryManager_;
    SaveManager saveManager_ {"mangapsp-progress.dat"};
    SettingsManager settingsManager_ {"mangapsp-settings.dat"};

    bool running_ = true;
    bool debugOverlay_ = false;
    bool hardwareTestOverlay_ = false;
    bool suspended_ = false;
    bool firstFrameStarted_ = false;
    bool firstFramePresented_ = false;
    bool loopAliveReported_ = false;
    Screen screen_ = Screen::Library;
    Screen returnScreen_ = Screen::Library;
    Screen optionsReturnScreen_ = Screen::Library;
    LibraryScreenState libraryState_;
    SeriesScreenState seriesState_;
    ReaderOverlayState readerOverlay_;
    BookmarkScreenState bookmarkState_;
    MenuScreenState optionsState_;
    MenuScreenState themeState_;
    std::vector<std::size_t> libraryOrder_;
    Uint32 lastPanTick_ = 0;
    Uint32 lastFrameTick_ = 0;
    Uint32 readerHudUntil_ = 0;
    Uint32 lastReaderInputTick_ = 0;
    float frameSeconds_ = 1.0f / 60.0f;
    std::uint64_t lastPageSwitchMicros_ = 0;
    std::uint64_t lastLibraryOpenMicros_ = 0;
    std::uint64_t lastChapterOpenMicros_ = 0;
    std::size_t suspendCount_ = 0;
    std::size_t resumeCount_ = 0;
    std::string lastError_;
};
