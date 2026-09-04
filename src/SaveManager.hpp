#pragma once

#include "Model.hpp"
#include "ReaderExperience.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class SavedFitMode {
    Page,
    Width
};

struct ReadingProgress {
    std::string seriesPath;
    std::string seriesName;
    std::string chapterPath;
    std::string chapterName;
    std::size_t pageIndex = 0;
    SavedFitMode fitMode = SavedFitMode::Page;
    ReadingDirection direction = ReadingDirection::RightToLeft;
    bool directionOverride = false;
    bool smartReading = true;
    SpreadMode spreadMode = SpreadMode::Auto;
    std::size_t logicalPosition = 0;
    bool favorite = false;
    bool completed = false;
    std::uint64_t lastReadOrder = 0;
};

struct Bookmark {
    std::string seriesPath;
    std::string seriesName;
    std::string chapterPath;
    std::string chapterName;
    std::size_t pageIndex = 0;
    std::size_t logicalPosition = 0;
    std::string label;
    std::uint64_t creationOrder = 0;
};

struct ProgressLocation {
    std::size_t seriesIndex = 0;
    std::size_t chapterIndex = 0;
    std::size_t pageIndex = 0;
    std::size_t logicalPosition = 0;
    bool recoveredChapter = false;
};

class SaveManager {
public:
    explicit SaveManager(std::string path);

    bool load();
    bool save();
    void update(ReadingProgress progress);
    void addBookmark(Bookmark bookmark);

    const ReadingProgress* find(const Series& series, const Chapter& chapter) const;
    const ReadingProgress* mostRecent() const;
    bool resolve(const std::vector<Series>& library, const ReadingProgress& progress,
                 ProgressLocation& location) const;
    bool resolve(const std::vector<Series>& library, const Bookmark& bookmark,
                 ProgressLocation& location) const;
    std::vector<std::size_t> recentlyReadSeries(const std::vector<Series>& library) const;
    std::uint64_t nextReadOrder() const;
    std::uint64_t nextBookmarkOrder() const;
    const std::string& lastError() const { return lastError_; }
    const std::vector<ReadingProgress>& entries() const { return entries_; }
    const std::vector<Bookmark>& bookmarks() const { return bookmarks_; }

private:
    std::string path_;
    std::vector<ReadingProgress> entries_;
    std::vector<Bookmark> bookmarks_;
    std::string lastError_;
};
