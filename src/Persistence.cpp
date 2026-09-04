#include "Persistence.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace {

char hexDigit(unsigned int value) {
    return static_cast<char>(value < 10 ? '0' + value : 'A' + value - 10);
}

int hexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

} // namespace

namespace Persistence {

std::string encodeField(const std::string& value) {
    std::string encoded;
    encoded.reserve(value.size());
    for (unsigned char byte : value) {
        if (byte == '%' || byte == '\t' || byte == '\r' || byte == '\n' || byte < 32u) {
            encoded.push_back('%');
            encoded.push_back(hexDigit((byte >> 4u) & 0x0fu));
            encoded.push_back(hexDigit(byte & 0x0fu));
        } else {
            encoded.push_back(static_cast<char>(byte));
        }
    }
    return encoded;
}

bool decodeField(const std::string& value, std::string& decoded) {
    decoded.clear();
    decoded.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '%') {
            decoded.push_back(value[index]);
            continue;
        }
        if (index + 2 >= value.size()) return false;
        const int high = hexValue(value[index + 1]);
        const int low = hexValue(value[index + 2]);
        if (high < 0 || low < 0) return false;
        decoded.push_back(static_cast<char>((high << 4) | low));
        index += 2;
    }
    return true;
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t tab = line.find('\t', start);
        fields.push_back(line.substr(start, tab == std::string::npos ? tab : tab - start));
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
    return fields;
}

bool atomicWrite(const std::string& path, const std::string& contents, std::string& error) {
    const std::string temporaryPath = path + ".tmp";
    const std::string backupPath = path + ".bak";

    {
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "Unable to open temporary save file.";
            return false;
        }
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        output.flush();
        if (!output) {
            error = "Unable to finish writing temporary save file.";
            output.close();
            std::remove(temporaryPath.c_str());
            return false;
        }
    }

    if (std::rename(temporaryPath.c_str(), path.c_str()) == 0) return true;

    std::remove(backupPath.c_str());
    const bool hadOriginal = std::rename(path.c_str(), backupPath.c_str()) == 0;
    if (std::rename(temporaryPath.c_str(), path.c_str()) == 0) {
        if (hadOriginal) std::remove(backupPath.c_str());
        return true;
    }

    if (hadOriginal) std::rename(backupPath.c_str(), path.c_str());
    std::remove(temporaryPath.c_str());
    error = "Unable to replace the existing save file.";
    return false;
}

} // namespace Persistence
