#pragma once

#include "FileScanner.hpp"
#include "Model.hpp"

#include <string>
#include <vector>

class LibraryManager {
public:
    explicit LibraryManager(std::string root = std::string());

    void setRoot(std::string root);
    void refresh(FileScanner::ScanDiagnostic diagnostic = nullptr);

    const std::string& root() const { return root_; }
    const std::vector<Series>& series() const { return series_; }
    bool rootAvailable() const { return rootAvailable_; }

private:
    std::string root_;
    std::vector<Series> series_;
    bool rootAvailable_ = false;
};
