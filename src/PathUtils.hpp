#pragma once

#include <string>

namespace PathUtils {

std::string join(const std::string& base, const std::string& child);
std::string filename(const std::string& path);
std::string stem(const std::string& path);
std::string extensionLower(const std::string& path);
std::string normalizedKey(const std::string& path);

bool exists(const std::string& path);
bool isDirectory(const std::string& path);
bool isSupportedImage(const std::string& path);
bool isCbz(const std::string& path);
bool isIgnoredEntry(const std::string& name);
bool isIgnoredArchivePath(const std::string& path);
bool isCoverFilename(const std::string& name);

} // namespace PathUtils
