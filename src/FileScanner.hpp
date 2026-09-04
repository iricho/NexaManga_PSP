#pragma once

#include "Model.hpp"

#include <string>
#include <vector>

namespace FileScanner {

using ScanDiagnostic = void (*)(const char* key, const char* value);

std::string findMangaRoot(ScanDiagnostic diagnostic = nullptr);
std::vector<Series> scanLibrary(const std::string& root,
                                ScanDiagnostic diagnostic = nullptr);
}
