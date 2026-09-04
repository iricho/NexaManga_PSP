#include "FileScanner.hpp"
#include "PageSource.hpp"
#include "PathUtils.hpp"
#include "SaveManager.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void write16(std::ofstream& output, std::uint16_t value) {
    output.put(static_cast<char>(value & 0xff));
    output.put(static_cast<char>((value >> 8) & 0xff));
}

void write32(std::ofstream& output, std::uint32_t value) {
    write16(output, static_cast<std::uint16_t>(value & 0xffff));
    write16(output, static_cast<std::uint16_t>((value >> 16) & 0xffff));
}

std::uint32_t crc32(const std::vector<std::uint8_t>& data) {
    std::uint32_t crc = 0xffffffffu;
    for (std::uint8_t byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

struct ZipEntry {
    std::string name;
    std::vector<std::uint8_t> data;
    std::uint32_t crc = 0;
    std::uint32_t offset = 0;
};

void writeStoredZip(const fs::path& path,
                    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> input) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    std::vector<ZipEntry> entries;
    for (auto& item : input) {
        ZipEntry entry;
        entry.name = std::move(item.first);
        entry.data = std::move(item.second);
        entry.crc = crc32(entry.data);
        entry.offset = static_cast<std::uint32_t>(output.tellp());
        write32(output, 0x04034b50u);
        write16(output, 20); write16(output, 0); write16(output, 0);
        write16(output, 0); write16(output, 0);
        write32(output, entry.crc);
        write32(output, static_cast<std::uint32_t>(entry.data.size()));
        write32(output, static_cast<std::uint32_t>(entry.data.size()));
        write16(output, static_cast<std::uint16_t>(entry.name.size()));
        write16(output, 0);
        output.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
        output.write(reinterpret_cast<const char*>(entry.data.data()),
                     static_cast<std::streamsize>(entry.data.size()));
        entries.push_back(std::move(entry));
    }

    const std::uint32_t centralOffset = static_cast<std::uint32_t>(output.tellp());
    for (const ZipEntry& entry : entries) {
        write32(output, 0x02014b50u);
        write16(output, 20); write16(output, 20); write16(output, 0); write16(output, 0);
        write16(output, 0); write16(output, 0); write32(output, entry.crc);
        write32(output, static_cast<std::uint32_t>(entry.data.size()));
        write32(output, static_cast<std::uint32_t>(entry.data.size()));
        write16(output, static_cast<std::uint16_t>(entry.name.size()));
        write16(output, 0); write16(output, 0); write16(output, 0); write16(output, 0);
        write32(output, 0); write32(output, entry.offset);
        output.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
    }
    const std::uint32_t centralSize = static_cast<std::uint32_t>(output.tellp()) - centralOffset;
    write32(output, 0x06054b50u);
    write16(output, 0); write16(output, 0);
    write16(output, static_cast<std::uint16_t>(entries.size()));
    write16(output, static_cast<std::uint16_t>(entries.size()));
    write32(output, centralSize); write32(output, centralOffset); write16(output, 0);
}

void touch(const fs::path& path) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.put('\0');
}

void testFolderSource(const fs::path& root) {
    touch(root / "folder" / "002.PNG");
    touch(root / "folder" / "001.jpg");
    const std::vector<Series> library = FileScanner::scanLibrary(root.generic_string());
    expect(library.size() == 1, "folder source series is discovered");
    if (library.empty()) return;
    const Chapter& chapter = library[0].chapters[0];
    FolderPageSource source(chapter);
    expect(source.available() && source.pageCount() == 2, "folder page source exposes count");
    expect(source.pageName(0) == "001.jpg", "folder source uses natural page order");
    PageData page;
    expect(source.readPage(0, page) && !page.filesystemPath.empty() && page.bytes.empty(),
           "folder source returns a path without copying encoded bytes");
    expect(page.identifier == chapter.pages[0].identifier, "folder page identifier is stable");
}

void testCbzSource(const fs::path& root) {
    const fs::path archive = root / "Archive 10.CBZ";
    writeStoredZip(archive, {
        {"nested/10.png", {10, 11}},
        {"nested/2.JPG", {2, 3, 4}},
        {"nested/2.jpg", {99}},
        {"nested/readme.txt", {1}},
        {"__MACOSX/1.jpg", {1}},
        {"nested/.hidden/3.jpg", {1}}
    });

    Chapter chapter = CbzPageSource::inspect(archive.generic_string());
    expect(chapter.available && chapter.pageCount() == 2, "CBZ filters junk and duplicate entries");
    expect(chapter.name == "Archive 10", "CBZ chapter name omits extension");
    if (chapter.pageCount() == 2) {
        expect(chapter.pages[0].logicalName == "nested/2.JPG" &&
               chapter.pages[1].logicalName == "nested/10.png", "nested CBZ pages sort naturally");
    } else {
        std::cerr << "CBZ inspect detail: " << chapter.error << " ("
                  << chapter.pageCount() << " pages)\n";
    }

    CbzPageSource source(chapter);
    PageData page;
    const bool read = source.available() && source.readPage(0, page);
    expect(read, "CBZ reads one selected entry");
    if (read) {
        expect(page.bytes == std::vector<std::uint8_t>({2, 3, 4}) && page.filesystemPath.empty(),
               "CBZ entry stays in memory only for the selected encoded page");
        expect(page.identifier.find("#nested/2.jpg") != std::string::npos,
               "CBZ page identifier combines archive and normalized entry");
    }
    source.prepareForSuspend();
    expect(!source.available(), "CBZ handle closes before suspend");
    expect(source.resumeAfterSuspend() && source.available() && source.readPage(0, page),
           "CBZ handle reopens and remains readable after resume");

    const fs::path empty = root / "Empty.cbz";
    writeStoredZip(empty, {{"notes.txt", {1, 2}}});
    const Chapter emptyChapter = CbzPageSource::inspect(empty.generic_string());
    expect(!emptyChapter.available && emptyChapter.pageCount() == 0 && !emptyChapter.error.empty(),
           "empty-image CBZ is rejected with an error");

    const fs::path corrupt = root / "Corrupt.cbz";
    { std::ofstream output(corrupt, std::ios::binary); output << "not a zip"; }
    const Chapter corruptChapter = CbzPageSource::inspect(corrupt.generic_string());
    expect(!corruptChapter.available && !corruptChapter.error.empty(),
           "corrupt archive is isolated without a crash");

    const fs::path corruptPageArchive = root / "Corrupt Page.cbz";
    const std::string corruptPageName = "001.jpg";
    writeStoredZip(corruptPageArchive, {{corruptPageName, {1, 2, 3, 4}}});
    {
        std::fstream file(corruptPageArchive, std::ios::in | std::ios::out | std::ios::binary);
        file.seekp(static_cast<std::streamoff>(30 + corruptPageName.size()));
        file.put(static_cast<char>(9));
    }
    const Chapter corruptPageChapter = CbzPageSource::inspect(corruptPageArchive.generic_string());
    CbzPageSource corruptPageSource(corruptPageChapter);
    expect(corruptPageChapter.available && !corruptPageSource.readPage(0, page) &&
           !corruptPageSource.lastError().empty(),
           "corrupt CBZ page payload reports an entry-level error");

    writeStoredZip(archive, {{"nested/2.JPG", {7, 8, 9}}});
    CbzPageSource changedSource(chapter);
    expect(!changedSource.readPage(1, page) && !changedSource.lastError().empty(),
           "missing changed archive entry reports a page-level error");
}

void testMixedLibraryAndProgress(const fs::path& root) {
    const fs::path series = root / "Mixed";
    touch(series / "Chapter 2" / "001.jpg");
    writeStoredZip(series / "Chapter 10.cbz", {{"001.jpg", {1}}});
    writeStoredZip(series / "Chapter 3.CBZ", {{"001.png", {2}}});

    const std::vector<Series> library = FileScanner::scanLibrary(root.generic_string());
    const Series* mixed = nullptr;
    for (const Series& item : library) if (item.name == "Mixed") mixed = &item;
    expect(mixed && mixed->chapters.size() == 3, "folder and CBZ chapters coexist");
    if (!mixed) return;
    expect(mixed->chapters[0].name == "Chapter 2" &&
           mixed->chapters[1].name == "Chapter 3" &&
           mixed->chapters[2].name == "Chapter 10", "mixed chapter sources sort naturally");

    ReadingProgress progress;
    progress.seriesPath = mixed->path;
    progress.seriesName = mixed->name;
    progress.chapterPath = mixed->chapters[1].path;
    progress.chapterName = mixed->chapters[1].name;
    progress.pageIndex = 0;
    SaveManager saves((root / "progress.dat").generic_string());
    saves.update(progress);
    expect(saves.find(*mixed, mixed->chapters[1]) != nullptr,
           "existing progress matching remains path-based for CBZ chapters");
}

} // namespace

int main() {
    const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() / ("mangapsp_sources_" + std::to_string(nonce));
    fs::create_directories(root);

    testFolderSource(root);
    testCbzSource(root);
    testMixedLibraryAndProgress(root);

    std::error_code error;
    fs::remove_all(root, error);
    expect(!error, "page source fixture cleanup");
    if (failures) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All page source tests passed\n";
    return 0;
}
