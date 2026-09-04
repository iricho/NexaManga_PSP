#include "FileScanner.hpp"

#include "BuildConfig.hpp"
#include "NaturalSort.hpp"
#include "PageSource.hpp"
#include "PathUtils.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif
#include <memory>
#include <unordered_set>
#include <utility>

namespace {

void emitDiagnostic(FileScanner::ScanDiagnostic diagnostic,
                    const char* key, const std::string& value) {
    if (diagnostic) diagnostic(key, value.c_str());
}

void emitCount(FileScanner::ScanDiagnostic diagnostic,
               const char* key, std::size_t value) {
    if (!diagnostic) return;
    char text[32];
    std::snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(value));
    diagnostic(key, text);
}

#ifndef _WIN32
struct DirectoryCloser {
    void operator()(DIR* directory) const {
        if (directory) closedir(directory);
    }
};

using DirectoryPtr = std::unique_ptr<DIR, DirectoryCloser>;
#endif

std::vector<std::string> directoryEntries(const std::string& path,
                                          FileScanner::ScanDiagnostic diagnostic) {
    std::vector<std::string> entries;
#ifdef _WIN32
    WIN32_FIND_DATAA data {};
    const std::string pattern = PathUtils::join(path, "*");
    const HANDLE search = FindFirstFileA(pattern.c_str(), &data);
    if (search == INVALID_HANDLE_VALUE) {
        char error[640];
        std::snprintf(error, sizeof(error), "%s: directory open failed (Windows error %lu)",
                      path.c_str(), static_cast<unsigned long>(GetLastError()));
        emitDiagnostic(diagnostic, "scan_error", error);
        return entries;
    }
    do entries.emplace_back(data.cFileName); while (FindNextFileA(search, &data));
    FindClose(search);
#else
    DirectoryPtr directory(opendir(path.c_str()));
    if (!directory) {
        const int errorNumber = errno;
        char error[640];
        std::snprintf(error, sizeof(error), "%s: opendir failed (errno %d: %s)",
                      path.c_str(), errorNumber, std::strerror(errorNumber));
        emitDiagnostic(diagnostic, "scan_error", error);
        return entries;
    }
    while (dirent* entry = readdir(directory.get())) entries.emplace_back(entry->d_name);
#endif
    return entries;
}

template <typename T, typename NameFunction>
void naturalSort(std::vector<T>& values, NameFunction name) {
    std::sort(values.begin(), values.end(), [&](const T& left, const T& right) {
        return NaturalSort::less(name(left), name(right));
    });
}

void removeDuplicatePages(std::vector<PageMetadata>& pages) {
    std::unordered_set<std::string> seen;
    pages.erase(std::remove_if(pages.begin(), pages.end(), [&](const PageMetadata& page) {
        return !seen.insert(page.identifier).second;
    }), pages.end());
}

std::vector<PageMetadata> listImageFiles(const std::string& path,
                                         FileScanner::ScanDiagnostic diagnostic) {
    std::vector<PageMetadata> pages;
    for (const std::string& name : directoryEntries(path, diagnostic)) {
        if (PathUtils::isIgnoredEntry(name) || !PathUtils::isSupportedImage(name)) continue;

        const std::string fullPath = PathUtils::join(path, name);
        if (!PathUtils::isDirectory(fullPath)) {
            PageMetadata page;
            page.logicalName = name;
            page.location = fullPath;
            page.identifier = PathUtils::normalizedKey(fullPath);
            pages.push_back(std::move(page));
        }
    }

    naturalSort(pages, [](const PageMetadata& page) { return page.logicalName; });
    removeDuplicatePages(pages);
    return pages;
}

#if !(MANGAPSP_DEVELOPMENT && MANGAPSP_DISABLE_CBZ)
std::vector<std::string> listCbzFiles(const std::string& path) {
    std::vector<std::string> archives;
    for (const std::string& name : directoryEntries(path, nullptr)) {
        if (PathUtils::isIgnoredEntry(name) || !PathUtils::isCbz(name)) continue;
        const std::string fullPath = PathUtils::join(path, name);
        if (!PathUtils::isDirectory(fullPath)) archives.push_back(fullPath);
    }
    naturalSort(archives, [](const std::string& archive) {
        return PathUtils::filename(archive);
    });
    std::unordered_set<std::string> seen;
    archives.erase(std::remove_if(archives.begin(), archives.end(),
        [&](const std::string& archive) {
            return !seen.insert(PathUtils::normalizedKey(archive)).second;
        }), archives.end());
    return archives;
}
#endif

std::vector<std::string> listSubdirectories(const std::string& path,
                                            FileScanner::ScanDiagnostic diagnostic) {
    std::vector<std::string> directories;
    for (const std::string& name : directoryEntries(path, diagnostic)) {
        if (PathUtils::isIgnoredEntry(name)) continue;
        if (PathUtils::isDirectory(PathUtils::join(path, name))) directories.push_back(name);
    }

    naturalSort(directories, [](const std::string& name) { return name; });

    std::unordered_set<std::string> seen;
    directories.erase(std::remove_if(directories.begin(), directories.end(),
        [&](const std::string& name) {
            return !seen.insert(PathUtils::normalizedKey(name)).second;
        }), directories.end());
    return directories;
}

std::string explicitCover(const std::vector<PageMetadata>& images) {
    for (const PageMetadata& image : images) {
        if (PathUtils::isCoverFilename(image.logicalName)) return image.location;
    }
    return {};
}

} // namespace

namespace FileScanner {

std::string findMangaRoot(ScanDiagnostic diagnostic) {
#ifdef __PSP__
    const char* candidates[] = {"ms0:/MANGA", "ef0:/MANGA", "./MANGA"};
#else
    const char* candidates[] = {"./MANGA"};
#endif

    for (const char* candidate : candidates) {
        emitDiagnostic(diagnostic, "root_candidate", candidate);
        const bool exists = PathUtils::isDirectory(candidate);
        if (diagnostic) diagnostic("root_candidate_exists", exists ? "1" : "0");
        if (exists) {
            emitDiagnostic(diagnostic, "root_selected", candidate);
            return candidate;
        }
    }
    emitDiagnostic(diagnostic, "root_fallback", candidates[0]);
    return candidates[0];
}

std::vector<Series> scanLibrary(const std::string& root, ScanDiagnostic diagnostic) {
    std::vector<Series> library;
    if (!PathUtils::isDirectory(root)) {
        emitDiagnostic(diagnostic, "scan_failure",
                       root + ": root does not exist or is not a directory");
        emitCount(diagnostic, "scan_series_count", 0);
        return library;
    }

    for (const std::string& seriesName : listSubdirectories(root, diagnostic)) {
        emitDiagnostic(diagnostic, "series_candidate", seriesName);
        Series series;
        series.name = seriesName;
        series.path = PathUtils::join(root, seriesName);

        const std::vector<std::string> chapterDirectories =
            listSubdirectories(series.path, diagnostic);
        std::vector<PageMetadata> topLevelPages = listImageFiles(series.path, diagnostic);
#if MANGAPSP_DEVELOPMENT && MANGAPSP_DISABLE_CBZ
        const std::vector<std::string> chapterArchives;
#else
        const std::vector<std::string> chapterArchives = listCbzFiles(series.path);
#endif
        series.coverPath = explicitCover(topLevelPages);

        if (topLevelPages.size() > 1 || !chapterDirectories.empty() || !chapterArchives.empty()) {
            topLevelPages.erase(std::remove_if(topLevelPages.begin(), topLevelPages.end(),
                [](const PageMetadata& page) {
                    return PathUtils::isCoverFilename(page.logicalName);
                }),
                topLevelPages.end());
        }

        if (!topLevelPages.empty()) {
            Chapter chapter;
            chapter.name = "Pages";
            chapter.path = series.path;
            chapter.sourceType = PageSourceType::Folder;
            chapter.pages = std::move(topLevelPages);
            series.chapters.push_back(std::move(chapter));
        }

        for (const std::string& chapterName : chapterDirectories) {
            emitDiagnostic(diagnostic, "chapter_candidate", chapterName);
            Chapter chapter;
            chapter.name = chapterName;
            chapter.path = PathUtils::join(series.path, chapterName);
            chapter.sourceType = PageSourceType::Folder;
            chapter.pages = listImageFiles(chapter.path, diagnostic);
            emitCount(diagnostic, "chapter_pages", chapter.pages.size());
            for (const PageMetadata& page : chapter.pages) {
                emitDiagnostic(diagnostic, "page_candidate", page.logicalName);
            }
            if (!chapter.pages.empty()) {
                series.chapters.push_back(std::move(chapter));
            } else {
                emitDiagnostic(diagnostic, "chapter_skipped",
                               chapterName + ": no supported JPG/JPEG/PNG pages");
            }
        }

#if !(MANGAPSP_DEVELOPMENT && MANGAPSP_DISABLE_CBZ)
        for (const std::string& archivePath : chapterArchives) {
            series.chapters.push_back(CbzPageSource::inspect(archivePath));
        }
#endif

        std::sort(series.chapters.begin(), series.chapters.end(),
            [](const Chapter& left, const Chapter& right) {
                const int order = NaturalSort::compare(left.name, right.name);
                if (order != 0) return order < 0;
                if (left.sourceType != right.sourceType) {
                    return left.sourceType == PageSourceType::Folder;
                }
                return PathUtils::normalizedKey(left.path) < PathUtils::normalizedKey(right.path);
            });
        if (series.coverPath.empty() && !series.chapters.empty()) {
            for (std::size_t index = 0; index < series.chapters.size(); ++index) {
                const Chapter& chapter = series.chapters[index];
                if (!chapter.pages.empty()) {
                    series.coverChapterIndex = index;
                    series.coverPageIndex = 0;
                    if (chapter.sourceType == PageSourceType::Folder) {
                        series.coverPath = chapter.pages.front().location;
                    }
                    break;
                }
            }
        }
        if (!series.chapters.empty()) {
            library.push_back(std::move(series));
        } else {
            emitDiagnostic(diagnostic, "series_skipped",
                           seriesName + ": no readable folder chapters or pages");
        }
    }

    naturalSort(library, [](const Series& series) { return series.name; });
    emitCount(diagnostic, "scan_series_count", library.size());
    return library;
}

} // namespace FileScanner
