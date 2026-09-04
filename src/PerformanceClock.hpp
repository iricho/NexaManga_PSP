#pragma once

#include "BuildConfig.hpp"

#include <cstdint>

#if MANGAPSP_ENABLE_METRICS && defined(__PSP__)
#include <pspkernel.h>
#elif MANGAPSP_ENABLE_METRICS
#include <chrono>
#endif

namespace PerformanceClock {

inline std::uint64_t nowMicros() {
#if !MANGAPSP_ENABLE_METRICS
    return 0;
#elif defined(__PSP__)
    return static_cast<std::uint64_t>(sceKernelGetSystemTimeWide());
#else
    using Clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now().time_since_epoch()).count());
#endif
}

inline std::uint64_t elapsedMicros(std::uint64_t start) {
#if !MANGAPSP_ENABLE_METRICS
    (void)start;
    return 0;
#else
    const std::uint64_t now = nowMicros();
    return now >= start ? now - start : 0;
#endif
}

} // namespace PerformanceClock
