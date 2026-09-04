#pragma once

#include <string>
#include <vector>
#include <limits>

enum class PageSourceType {
    Folder,
    Cbz
};

struct PageMetadata {
    std::string logicalName;
    std::string identifier;
    std::string location;
    std::size_t uncompressedSize = 0;
};

struct Chapter {
    std::string name;
    std::string path;
    PageSourceType sourceType = PageSourceType::Folder;
    std::vector<PageMetadata> pages;
    bool available = true;
    std::string error;

    std::size_t pageCount() const { return pages.size(); }
};

struct Series {
    std::string name;
    std::string path;
    std::string coverPath;
    std::size_t coverChapterIndex = std::numeric_limits<std::size_t>::max();
    std::size_t coverPageIndex = 0;
    std::vector<Chapter> chapters;

    bool hasPageCover() const { return coverChapterIndex < chapters.size(); }
};
