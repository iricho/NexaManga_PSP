#include "SaveManager.hpp"

#include "PathUtils.hpp"
#include "Persistence.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace {

bool parseUnsigned(const std::string& text, std::uint64_t& value) {
    if (text.empty() || text.front() == '-') return false;
    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
    if (errno != 0 || !end || *end != '\0') return false;
    value = static_cast<std::uint64_t>(parsed);
    return true;
}

bool sameIdentity(const ReadingProgress& entry, const Series& series, const Chapter& chapter) {
    const bool pathMatch = PathUtils::normalizedKey(entry.seriesPath) ==
                           PathUtils::normalizedKey(series.path) &&
                           PathUtils::normalizedKey(entry.chapterPath) ==
                           PathUtils::normalizedKey(chapter.path);
    if (pathMatch) return true;

    // Fallback keeps progress useful when only a manga root or parent folder moved.
    return PathUtils::normalizedKey(entry.seriesName) == PathUtils::normalizedKey(series.name) &&
           PathUtils::normalizedKey(entry.chapterName) == PathUtils::normalizedKey(chapter.name);
}

bool sameSeries(const std::string& path, const std::string& name, const Series& series) {
    return PathUtils::normalizedKey(path) == PathUtils::normalizedKey(series.path) ||
           PathUtils::normalizedKey(name) == PathUtils::normalizedKey(series.name);
}

bool sameChapter(const std::string& path, const std::string& name, const Chapter& chapter) {
    return PathUtils::normalizedKey(path) == PathUtils::normalizedKey(chapter.path) ||
           PathUtils::normalizedKey(name) == PathUtils::normalizedKey(chapter.name);
}

const char* spreadName(SpreadMode mode) {
    if (mode == SpreadMode::FullSpread) return "full";
    if (mode == SpreadMode::SplitSpread) return "split";
    return "auto";
}

bool parseSpread(const std::string& value, SpreadMode& mode) {
    if (value == "auto") mode = SpreadMode::Auto;
    else if (value == "full") mode = SpreadMode::FullSpread;
    else if (value == "split") mode = SpreadMode::SplitSpread;
    else return false;
    return true;
}

} // namespace

SaveManager::SaveManager(std::string path)
    : path_(std::move(path)) {
}

bool SaveManager::load() {
    entries_.clear();
    bookmarks_.clear();
    lastError_.clear();

    std::ifstream input(path_, std::ios::binary);
    if (!input) return true;

    std::string line;
    if (!std::getline(input, line)) {
        lastError_ = "Progress file is empty.";
        return false;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const bool version1 = line == "MANGAPSP_PROGRESS\t1";
    const bool version2 = line == "MANGAPSP_PROGRESS\t2";
    if (!version1 && !version2) {
        lastError_ = "Unsupported or corrupt progress header.";
        return false;
    }

    std::size_t skipped = 0;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const std::vector<std::string> fields = Persistence::splitTabs(line);
        if (fields.empty()) {
            ++skipped;
            continue;
        }

        if (version2 && fields[0] == "B") {
            Bookmark bookmark;
            std::uint64_t page = 0;
            std::uint64_t logical = 0;
            std::uint64_t order = 0;
            if (fields.size() != 9 ||
                !Persistence::decodeField(fields[1], bookmark.seriesPath) ||
                !Persistence::decodeField(fields[2], bookmark.seriesName) ||
                !Persistence::decodeField(fields[3], bookmark.chapterPath) ||
                !Persistence::decodeField(fields[4], bookmark.chapterName) ||
                !parseUnsigned(fields[5], page) || !parseUnsigned(fields[6], logical) ||
                !Persistence::decodeField(fields[7], bookmark.label) ||
                !parseUnsigned(fields[8], order) ||
                page > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
                logical > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
                bookmark.seriesPath.empty() || bookmark.chapterPath.empty()) {
                ++skipped;
                continue;
            }
            bookmark.pageIndex = static_cast<std::size_t>(page);
            bookmark.logicalPosition = static_cast<std::size_t>(logical);
            bookmark.creationOrder = order;
            bookmarks_.push_back(std::move(bookmark));
            continue;
        }

        if (fields[0] != "P" || (version1 && fields.size() != 11) ||
            (version2 && fields.size() != 15)) {
            ++skipped;
            continue;
        }

        ReadingProgress progress;
        std::uint64_t page = 0;
        std::uint64_t logical = 0;
        std::uint64_t order = 0;
        const std::size_t fitField = version1 ? 6u : 7u;
        const std::size_t directionField = version1 ? 7u : 8u;
        const std::size_t favoriteField = version1 ? 8u : 12u;
        const std::size_t completedField = version1 ? 9u : 13u;
        const std::size_t orderField = version1 ? 10u : 14u;
        if (!Persistence::decodeField(fields[1], progress.seriesPath) ||
            !Persistence::decodeField(fields[2], progress.seriesName) ||
            !Persistence::decodeField(fields[3], progress.chapterPath) ||
            !Persistence::decodeField(fields[4], progress.chapterName) ||
            !parseUnsigned(fields[5], page) ||
            (!version1 && !parseUnsigned(fields[6], logical)) ||
            (fields[fitField] != "page" && fields[fitField] != "width") ||
            (fields[directionField] != "rtl" && fields[directionField] != "ltr") ||
            (fields[favoriteField] != "0" && fields[favoriteField] != "1") ||
            (fields[completedField] != "0" && fields[completedField] != "1") ||
            !parseUnsigned(fields[orderField], order) ||
            page > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
            logical > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
            progress.seriesPath.empty() || progress.seriesName.empty() ||
            progress.chapterPath.empty() || progress.chapterName.empty()) {
            ++skipped;
            continue;
        }

        progress.pageIndex = static_cast<std::size_t>(page);
        progress.logicalPosition = static_cast<std::size_t>(logical);
        progress.fitMode = fields[fitField] == "width" ? SavedFitMode::Width : SavedFitMode::Page;
        progress.direction = fields[directionField] == "ltr" ? ReadingDirection::LeftToRight
                                                 : ReadingDirection::RightToLeft;
        if (version2) {
            if ((fields[9] != "0" && fields[9] != "1") ||
                (fields[10] != "0" && fields[10] != "1") ||
                !parseSpread(fields[11], progress.spreadMode)) {
                ++skipped;
                continue;
            }
            progress.directionOverride = fields[9] == "1";
            progress.smartReading = fields[10] == "1";
        }
        progress.favorite = fields[favoriteField] == "1";
        progress.completed = fields[completedField] == "1";
        progress.lastReadOrder = order;
        update(std::move(progress));
    }

    if (skipped > 0) {
        std::ostringstream warning;
        warning << "Skipped " << skipped << " corrupt progress entr" << (skipped == 1 ? "y." : "ies.");
        lastError_ = warning.str();
    }
    return true;
}

bool SaveManager::save() {
    std::ostringstream output;
    output << "MANGAPSP_PROGRESS\t2\n";
    for (const ReadingProgress& progress : entries_) {
        output << "P\t" << Persistence::encodeField(progress.seriesPath)
               << '\t' << Persistence::encodeField(progress.seriesName)
               << '\t' << Persistence::encodeField(progress.chapterPath)
               << '\t' << Persistence::encodeField(progress.chapterName)
               << '\t' << progress.pageIndex
               << '\t' << progress.logicalPosition
               << '\t' << (progress.fitMode == SavedFitMode::Width ? "width" : "page")
               << '\t' << (progress.direction == ReadingDirection::LeftToRight ? "ltr" : "rtl")
               << '\t' << (progress.directionOverride ? 1 : 0)
               << '\t' << (progress.smartReading ? 1 : 0)
               << '\t' << spreadName(progress.spreadMode)
               << '\t' << (progress.favorite ? 1 : 0)
               << '\t' << (progress.completed ? 1 : 0)
               << '\t' << progress.lastReadOrder << '\n';
    }
    for (const Bookmark& bookmark : bookmarks_) {
        output << "B\t" << Persistence::encodeField(bookmark.seriesPath)
               << '\t' << Persistence::encodeField(bookmark.seriesName)
               << '\t' << Persistence::encodeField(bookmark.chapterPath)
               << '\t' << Persistence::encodeField(bookmark.chapterName)
               << '\t' << bookmark.pageIndex
               << '\t' << bookmark.logicalPosition
               << '\t' << Persistence::encodeField(bookmark.label)
               << '\t' << bookmark.creationOrder << '\n';
    }
    lastError_.clear();
    return Persistence::atomicWrite(path_, output.str(), lastError_);
}

void SaveManager::addBookmark(Bookmark bookmark) {
    bookmarks_.push_back(std::move(bookmark));
}

void SaveManager::update(ReadingProgress progress) {
    auto existing = std::find_if(entries_.begin(), entries_.end(), [&](const ReadingProgress& entry) {
        return PathUtils::normalizedKey(entry.seriesPath) == PathUtils::normalizedKey(progress.seriesPath) &&
               PathUtils::normalizedKey(entry.chapterPath) == PathUtils::normalizedKey(progress.chapterPath);
    });
    if (existing == entries_.end()) entries_.push_back(std::move(progress));
    else *existing = std::move(progress);
}

const ReadingProgress* SaveManager::find(const Series& series, const Chapter& chapter) const {
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const ReadingProgress& entry) {
        return sameIdentity(entry, series, chapter);
    });
    return found == entries_.end() ? nullptr : &*found;
}

const ReadingProgress* SaveManager::mostRecent() const {
    if (entries_.empty()) return nullptr;
    return &*std::max_element(entries_.begin(), entries_.end(),
        [](const ReadingProgress& left, const ReadingProgress& right) {
            return left.lastReadOrder < right.lastReadOrder;
        });
}

bool SaveManager::resolve(const std::vector<Series>& library,
                          const ReadingProgress& progress,
                          ProgressLocation& location) const {
    std::size_t seriesIndex = library.size();
    for (std::size_t i = 0; i < library.size(); ++i) {
        if (sameSeries(progress.seriesPath, progress.seriesName, library[i])) {
            seriesIndex = i;
            break;
        }
    }
    if (seriesIndex == library.size() || library[seriesIndex].chapters.empty()) return false;

    const Series& series = library[seriesIndex];
    std::size_t chapterIndex = series.chapters.size();
    for (std::size_t i = 0; i < series.chapters.size(); ++i) {
        if (sameChapter(progress.chapterPath, progress.chapterName, series.chapters[i])) {
            chapterIndex = i;
            break;
        }
    }
    bool recovered = false;
    if (chapterIndex == series.chapters.size()) {
        recovered = true;
        std::uint64_t newest = 0;
        for (std::size_t i = 0; i < series.chapters.size(); ++i) {
            if (const ReadingProgress* candidate = find(series, series.chapters[i])) {
                if (chapterIndex == series.chapters.size() || candidate->lastReadOrder >= newest) {
                    newest = candidate->lastReadOrder;
                    chapterIndex = i;
                }
            }
        }
        if (chapterIndex == series.chapters.size()) chapterIndex = 0;
    }
    const std::size_t count = series.chapters[chapterIndex].pageCount();
    if (count == 0) return false;
    location = {seriesIndex, chapterIndex, std::min(progress.pageIndex, count - 1u),
                progress.logicalPosition, recovered};
    return true;
}

bool SaveManager::resolve(const std::vector<Series>& library,
                          const Bookmark& bookmark,
                          ProgressLocation& location) const {
    ReadingProgress progress;
    progress.seriesPath = bookmark.seriesPath;
    progress.seriesName = bookmark.seriesName;
    progress.chapterPath = bookmark.chapterPath;
    progress.chapterName = bookmark.chapterName;
    progress.pageIndex = bookmark.pageIndex;
    progress.logicalPosition = bookmark.logicalPosition;
    return resolve(library, progress, location);
}

std::vector<std::size_t> SaveManager::recentlyReadSeries(
    const std::vector<Series>& library) const {
    std::vector<std::pair<std::uint64_t, std::size_t>> ranked;
    ranked.reserve(library.size());
    for (std::size_t i = 0; i < library.size(); ++i) {
        std::uint64_t newest = 0;
        for (const ReadingProgress& progress : entries_) {
            if (sameSeries(progress.seriesPath, progress.seriesName, library[i])) {
                newest = std::max(newest, progress.lastReadOrder);
            }
        }
        if (newest > 0) ranked.push_back({newest, i});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
        return left.first != right.first ? left.first > right.first : left.second < right.second;
    });
    std::vector<std::size_t> result;
    result.reserve(ranked.size());
    for (const auto& item : ranked) result.push_back(item.second);
    return result;
}

std::uint64_t SaveManager::nextReadOrder() const {
    const ReadingProgress* recent = mostRecent();
    return recent && recent->lastReadOrder < std::numeric_limits<std::uint64_t>::max()
        ? recent->lastReadOrder + 1u : 1u;
}

std::uint64_t SaveManager::nextBookmarkOrder() const {
    std::uint64_t newest = 0;
    for (const Bookmark& bookmark : bookmarks_) newest = std::max(newest, bookmark.creationOrder);
    return newest < std::numeric_limits<std::uint64_t>::max() ? newest + 1u : newest;
}
