#pragma once

#include "Model.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct PageData {
    std::string logicalName;
    std::string identifier;
    std::string filesystemPath;
    std::vector<std::uint8_t> bytes;
};

class PageSource {
public:
    virtual ~PageSource() = default;

    virtual std::size_t pageCount() const = 0;
    virtual const std::string& pageName(std::size_t index) const = 0;
    virtual const std::string& pageId(std::size_t index) const = 0;
    virtual PageSourceType sourceType() const = 0;
    virtual const std::string& sourcePath() const = 0;
    virtual bool available() const = 0;
    virtual const std::string& lastError() const = 0;
    virtual bool readPage(std::size_t index, PageData& data) = 0;
    virtual void prepareForSuspend() {}
    virtual bool resumeAfterSuspend() { return available(); }
};

class FolderPageSource final : public PageSource {
public:
    explicit FolderPageSource(const Chapter& chapter);

    std::size_t pageCount() const override;
    const std::string& pageName(std::size_t index) const override;
    const std::string& pageId(std::size_t index) const override;
    PageSourceType sourceType() const override { return PageSourceType::Folder; }
    const std::string& sourcePath() const override { return path_; }
    bool available() const override { return available_; }
    const std::string& lastError() const override { return lastError_; }
    bool readPage(std::size_t index, PageData& data) override;

private:
    std::string path_;
    std::vector<PageMetadata> pages_;
    bool available_ = false;
    std::string lastError_;
};

class CbzPageSource final : public PageSource {
public:
    explicit CbzPageSource(const Chapter& chapter);
    ~CbzPageSource() override;

    CbzPageSource(const CbzPageSource&) = delete;
    CbzPageSource& operator=(const CbzPageSource&) = delete;

    static Chapter inspect(const std::string& archivePath);

    std::size_t pageCount() const override;
    const std::string& pageName(std::size_t index) const override;
    const std::string& pageId(std::size_t index) const override;
    PageSourceType sourceType() const override { return PageSourceType::Cbz; }
    const std::string& sourcePath() const override { return path_; }
    bool available() const override { return available_; }
    const std::string& lastError() const override { return lastError_; }
    bool readPage(std::size_t index, PageData& data) override;
    void prepareForSuspend() override;
    bool resumeAfterSuspend() override;

private:
    bool open();
    void close();

    std::string path_;
    std::vector<PageMetadata> pages_;
    void* reader_ = nullptr;
    bool available_ = false;
    bool suspended_ = false;
    std::string lastError_;
};

std::unique_ptr<PageSource> createPageSource(const Chapter& chapter);
