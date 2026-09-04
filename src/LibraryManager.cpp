#include "LibraryManager.hpp"

#include "FileScanner.hpp"
#include "PathUtils.hpp"

#include <utility>

LibraryManager::LibraryManager(std::string root)
    : root_(std::move(root)) {
}

void LibraryManager::setRoot(std::string root) {
    root_ = std::move(root);
    series_.clear();
    rootAvailable_ = false;
}

void LibraryManager::refresh(FileScanner::ScanDiagnostic diagnostic) {
    rootAvailable_ = PathUtils::isDirectory(root_);
    if (diagnostic) diagnostic("root_exists", rootAvailable_ ? "1" : "0");
    if (!rootAvailable_) {
        series_.clear();
        if (diagnostic) {
            diagnostic("scan_failure", "selected manga root does not exist");
            diagnostic("scan_series_count", "0");
        }
        return;
    }
    series_ = FileScanner::scanLibrary(root_, diagnostic);
}
