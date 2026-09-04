#include "PathUtils.hpp"

#include <algorithm>
#include <sys/stat.h>

namespace {

std::string asciiLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        if (c >= 'A' && c <= 'Z') return static_cast<char>(c + ('a' - 'A'));
        return static_cast<char>(c);
    });
    return value;
}

} // namespace

namespace PathUtils {

std::string join(const std::string& base, const std::string& child) {
    if (base.empty()) return child;
    if (child.empty()) return base;
    if (base.back() == '/' || base.back() == '\\') return base + child;
    return base + "/" + child;
}

std::string filename(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string stem(const std::string& path) {
    const std::string name = filename(path);
    const std::size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

std::string extensionLower(const std::string& path) {
    const std::string name = filename(path);
    const std::size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? std::string() : asciiLower(name.substr(dot));
}

std::string normalizedKey(const std::string& path) {
    std::string key = asciiLower(path);
    std::replace(key.begin(), key.end(), '\\', '/');
    while (key.size() > 1 && key.back() == '/') key.pop_back();
    return key;
}

bool exists(const std::string& path) {
    struct stat status {};
    return stat(path.c_str(), &status) == 0;
}

bool isDirectory(const std::string& path) {
    struct stat status {};
#ifdef _WIN32
    return stat(path.c_str(), &status) == 0 && (status.st_mode & _S_IFDIR) != 0;
#else
    return stat(path.c_str(), &status) == 0 && S_ISDIR(status.st_mode);
#endif
}

bool isSupportedImage(const std::string& path) {
    const std::string extension = extensionLower(path);
    return extension == ".jpg" || extension == ".jpeg" || extension == ".png";
}

bool isCbz(const std::string& path) {
    return extensionLower(path) == ".cbz";
}

bool isIgnoredEntry(const std::string& name) {
    if (name.empty() || name == "." || name == "..") return true;
    if (name.front() == '.' || name.front() == '$') return true;

    const std::string folded = asciiLower(name);
    return folded == "system volume information" || folded == "thumbs.db" ||
           folded == "desktop.ini" || folded == "__macosx";
}

bool isIgnoredArchivePath(const std::string& path) {
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t separator = path.find_first_of("/\\", start);
        const std::string component = path.substr(
            start, separator == std::string::npos ? std::string::npos : separator - start);
        if (isIgnoredEntry(component)) return true;
        if (separator == std::string::npos) break;
        start = separator + 1;
    }
    return false;
}

bool isCoverFilename(const std::string& name) {
    const std::string folded = asciiLower(filename(name));
    return folded == "cover.jpg" || folded == "cover.jpeg" || folded == "cover.png" ||
           folded == "folder.jpg" || folded == "folder.jpeg" || folded == "folder.png";
}

} // namespace PathUtils
