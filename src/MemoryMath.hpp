#pragma once

#include <cstddef>
#include <limits>

namespace MemoryMath {

inline std::size_t saturatedAdd(std::size_t left, std::size_t right) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return std::numeric_limits<std::size_t>::max();
    }
    return left + right;
}

inline std::size_t saturatedMultiply(std::size_t left, std::size_t right) {
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
        return std::numeric_limits<std::size_t>::max();
    }
    return left * right;
}

inline std::size_t rgb565Pitch(std::size_t width) {
    const std::size_t bytes = saturatedMultiply(width, 2u);
    if (bytes > std::numeric_limits<std::size_t>::max() - 3u) {
        return std::numeric_limits<std::size_t>::max();
    }
    return (bytes + 3u) & ~static_cast<std::size_t>(3u);
}

inline std::size_t rgb565Bytes(std::size_t width, std::size_t height) {
    return saturatedMultiply(rgb565Pitch(width), height);
}

} // namespace MemoryMath
