#include "PageSource.hpp"

#include "BuildConfig.hpp"
#include "NaturalSort.hpp"
#include "Log.hpp"
#include "PathUtils.hpp"

#include <algorithm>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <utility>

#if !(MANGAPSP_DEVELOPMENT && MANGAPSP_DISABLE_CBZ)
extern "C" {
#include <mz.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>
}
#endif

namespace {

const std::string& emptyString() {
    static const std::string empty;
    return empty;
}

const std::string& pageField(const std::vector<PageMetadata>& pages,
                             std::size_t index, bool identifier) {
    if (index >= pages.size()) return emptyString();
    return identifier ? pages[index].identifier : pages[index].logicalName;
}

#if !(MANGAPSP_DEVELOPMENT && MANGAPSP_DISABLE_CBZ)
std::string archivePageId(const std::string& archive, const std::string& entry) {
    return PathUtils::normalizedKey(archive) + "#" + PathUtils::normalizedKey(entry);
}

std::string archiveError(const char* operation, int32_t code) {
    std::ostringstream message;
    message << operation << " (minizip error " << code << ")";
    return message.str();
}

void* createArchiveReader() {
#if MZ_VERSION_BUILD >= 040000
    return mz_zip_reader_create();
#else
    void* reader = nullptr;
    mz_zip_reader_create(&reader);
    return reader;
#endif
}
#endif

} // namespace

FolderPageSource::FolderPageSource(const Chapter& chapter)
    : path_(chapter.path), pages_(chapter.pages),
      available_(chapter.available && !chapter.pages.empty()),
      lastError_(chapter.error) {
    if (!available_ && lastError_.empty()) lastError_ = "Folder chapter has no readable pages.";
}

std::size_t FolderPageSource::pageCount() const { return pages_.size(); }

const std::string& FolderPageSource::pageName(std::size_t index) const {
    return pageField(pages_, index, false);
}

const std::string& FolderPageSource::pageId(std::size_t index) const {
    return pageField(pages_, index, true);
}

bool FolderPageSource::readPage(std::size_t index, PageData& data) {
    data = {};
    if (index >= pages_.size()) {
        lastError_ = "Folder page index is out of range.";
        return false;
    }
    const PageMetadata& page = pages_[index];
    if (!PathUtils::exists(page.location) || PathUtils::isDirectory(page.location)) {
        lastError_ = "Folder page is no longer available: " + page.location;
        return false;
    }
    data.logicalName = page.logicalName;
    data.identifier = page.identifier;
    data.filesystemPath = page.location;
    lastError_.clear();
    return true;
}

#if !(MANGAPSP_DEVELOPMENT && MANGAPSP_DISABLE_CBZ)
Chapter CbzPageSource::inspect(const std::string& archivePath) {
    Chapter chapter;
    chapter.name = PathUtils::stem(archivePath);
    chapter.path = archivePath;
    chapter.sourceType = PageSourceType::Cbz;

    MANGAPSP_LOG_RUNTIME_INFO("CBZ archive open: " + archivePath);
    void* reader = createArchiveReader();
    if (!reader) {
        chapter.available = false;
        chapter.error = "Could not allocate the CBZ reader.";
        Log::error(archivePath + ": " + chapter.error);
        return chapter;
    }

    int32_t error = mz_zip_reader_open_file(reader, archivePath.c_str());
    if (error != MZ_OK) {
        chapter.available = false;
        chapter.error = archiveError("Could not open CBZ", error);
        Log::error(archivePath + ": " + chapter.error);
        mz_zip_reader_delete(&reader);
        return chapter;
    }

    std::unordered_set<std::string> seen;
    error = mz_zip_reader_goto_first_entry(reader);
    while (error == MZ_OK) {
        mz_zip_file* info = nullptr;
        const int32_t infoError = mz_zip_reader_entry_get_info(reader, &info);
        if (infoError != MZ_OK || !info || !info->filename) {
            chapter.available = false;
            chapter.error = archiveError("Could not inspect CBZ entry", infoError);
            break;
        }

        const std::string entry = info->filename;
        if (mz_zip_reader_entry_is_dir(reader) != MZ_OK &&
            !PathUtils::isIgnoredArchivePath(entry) &&
            PathUtils::isSupportedImage(entry)) {
            const std::string key = PathUtils::normalizedKey(entry);
            if (seen.insert(key).second) {
                PageMetadata page;
                page.logicalName = entry;
                page.location = entry;
                page.identifier = archivePageId(archivePath, entry);
                if (info->uncompressed_size > 0 &&
                    static_cast<uint64_t>(info->uncompressed_size) <=
                        static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
                    page.uncompressedSize = static_cast<std::size_t>(info->uncompressed_size);
                }
                chapter.pages.push_back(std::move(page));
            }
        }
        error = mz_zip_reader_goto_next_entry(reader);
    }

    if (chapter.available && error != MZ_END_OF_LIST) {
        chapter.available = false;
        chapter.error = archiveError("Could not enumerate CBZ", error);
    }

    mz_zip_reader_delete(&reader);

    std::sort(chapter.pages.begin(), chapter.pages.end(),
        [](const PageMetadata& left, const PageMetadata& right) {
            return NaturalSort::less(left.logicalName, right.logicalName);
        });

    if (chapter.pages.empty()) {
        chapter.available = false;
        if (chapter.error.empty()) chapter.error = "CBZ contains no supported image pages.";
    }
    if (!chapter.available) chapter.pages.clear();
    if (chapter.available) {
        MANGAPSP_LOG_RUNTIME_INFO("CBZ entry count: " +
                                  std::to_string(chapter.pages.size()) +
                                  " in " + archivePath);
    } else {
        Log::warning(archivePath + ": " + chapter.error);
    }
    return chapter;
}

CbzPageSource::CbzPageSource(const Chapter& chapter)
    : path_(chapter.path), pages_(chapter.pages), lastError_(chapter.error) {
    if (!chapter.available || pages_.empty()) {
        if (lastError_.empty()) lastError_ = "CBZ contains no readable pages.";
        return;
    }
    open();
}

CbzPageSource::~CbzPageSource() { close(); }

bool CbzPageSource::open() {
    close();
    reader_ = createArchiveReader();
    if (!reader_) {
        lastError_ = "Could not allocate the CBZ reader.";
        return false;
    }
    const int32_t error = mz_zip_reader_open_file(reader_, path_.c_str());
    if (error != MZ_OK) {
        lastError_ = archiveError("Could not open CBZ", error);
        close();
        return false;
    }
    available_ = true;
    lastError_.clear();
    MANGAPSP_LOG_RUNTIME_INFO("CBZ archive reopened for selected page reads: " + path_);
    return true;
}

void CbzPageSource::close() {
    if (reader_) {
        mz_zip_reader_delete(&reader_);
    }
    available_ = false;
}

std::size_t CbzPageSource::pageCount() const { return pages_.size(); }

const std::string& CbzPageSource::pageName(std::size_t index) const {
    return pageField(pages_, index, false);
}

const std::string& CbzPageSource::pageId(std::size_t index) const {
    return pageField(pages_, index, true);
}

bool CbzPageSource::readPage(std::size_t index, PageData& data) {
    data = {};
    if (!reader_ || !available_) {
        if (lastError_.empty()) lastError_ = "CBZ reader is not available.";
        return false;
    }
    if (index >= pages_.size()) {
        lastError_ = "CBZ page index is out of range.";
        return false;
    }

    const PageMetadata& page = pages_[index];
    int32_t error = mz_zip_reader_locate_entry(reader_, page.location.c_str(), 0);
    if (error != MZ_OK) {
        lastError_ = archiveError("Could not locate CBZ page", error);
        Log::error(path_ + ": " + lastError_);
        return false;
    }

    const int32_t length = mz_zip_reader_entry_save_buffer_length(reader_);
    if (length < 0) {
        lastError_ = archiveError("Could not size CBZ page", length);
        Log::error(path_ + ": " + lastError_);
        return false;
    }
    if (length == 0) {
        lastError_ = "CBZ page is empty: " + page.logicalName;
        return false;
    }

    data.bytes.resize(static_cast<std::size_t>(length));
    error = mz_zip_reader_entry_save_buffer(reader_, data.bytes.data(), length);
    if (error != MZ_OK) {
        data = {};
        lastError_ = archiveError("Could not read CBZ page", error);
        Log::error(path_ + ": " + lastError_);
        return false;
    }
    data.logicalName = page.logicalName;
    data.identifier = page.identifier;
    lastError_.clear();
    return true;
}

void CbzPageSource::prepareForSuspend() {
    suspended_ = true;
    close();
}

bool CbzPageSource::resumeAfterSuspend() {
    if (!suspended_) return available_;
    suspended_ = false;
    return open();
}
#endif

std::unique_ptr<PageSource> createPageSource(const Chapter& chapter) {
#if !(MANGAPSP_DEVELOPMENT && MANGAPSP_DISABLE_CBZ)
    if (chapter.sourceType == PageSourceType::Cbz) {
        return std::unique_ptr<PageSource>(new CbzPageSource(chapter));
    }
#else
    if (chapter.sourceType == PageSourceType::Cbz) return nullptr;
#endif
    return std::unique_ptr<PageSource>(new FolderPageSource(chapter));
}
