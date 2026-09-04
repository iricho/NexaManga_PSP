#include "MemoryBudget.hpp"

#include <algorithm>

namespace MemoryBudget {

PspMemoryClass classifyPsp(std::size_t freeUserBytes) {
#if defined(MANGAPSP_FORCE_PSP_32MB_PROFILE)
    (void)freeUserBytes;
    return PspMemoryClass::Standard32MiB;
#elif defined(MANGAPSP_FORCE_PSP_64MB_PROFILE)
    (void)freeUserBytes;
    return PspMemoryClass::Enhanced64MiB;
#else
    return freeUserBytes >= EnhancedFreeMemoryThreshold
        ? PspMemoryClass::Enhanced64MiB : PspMemoryClass::Standard32MiB;
#endif
}

std::size_t imageBudget(PspMemoryClass memoryClass) {
    return memoryClass == PspMemoryClass::Enhanced64MiB
        ? EnhancedImageBudget : StandardImageBudget;
}

std::size_t clampRequested(std::size_t requestedBytes, PspMemoryClass memoryClass) {
    const std::size_t safeMaximum = imageBudget(memoryClass);
    return requestedBytes == 0 ? safeMaximum : std::min(requestedBytes, safeMaximum);
}

const char* label(PspMemoryClass memoryClass) {
    return memoryClass == PspMemoryClass::Enhanced64MiB
        ? "64 MiB PSP profile" : "32 MiB PSP profile";
}

} // namespace MemoryBudget

