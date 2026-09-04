#pragma once

#include <string>
#include <vector>

namespace Persistence {

std::string encodeField(const std::string& value);
bool decodeField(const std::string& value, std::string& decoded);
std::vector<std::string> splitTabs(const std::string& line);
bool atomicWrite(const std::string& path, const std::string& contents, std::string& error);

} // namespace Persistence
