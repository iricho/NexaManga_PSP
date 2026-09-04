#pragma once

#include <cstddef>

namespace MemoryBudget {

enum class PspMemoryClass {
    Standard32MiB,
    Enhanced64MiB
};

constexpr std::size_t MiB = 1024u * 1024u;
constexpr std::size_t StandardImageBudget = 16u * MiB;
constexpr std::size_t EnhancedImageBudget = 36u * MiB;
constexpr std::size_t EnhancedFreeMemoryThreshold = 40u * MiB;

PspMemoryClass classifyPsp(std::size_t freeUserBytes);
std::size_t imageBudget(PspMemoryClass memoryClass);
std::size_t clampRequested(std::size_t requestedBytes, PspMemoryClass memoryClass);
const char* label(PspMemoryClass memoryClass);

} // namespace MemoryBudget

