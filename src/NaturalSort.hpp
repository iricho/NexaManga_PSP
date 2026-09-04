#pragma once

#include <string>

namespace NaturalSort {

int compare(const std::string& left, const std::string& right);

inline bool less(const std::string& left, const std::string& right) {
    return compare(left, right) < 0;
}

} // namespace NaturalSort
