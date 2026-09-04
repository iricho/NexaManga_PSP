#pragma once

#include <cstddef>

namespace Platform {

struct LifecycleEvents {
    bool exitRequested = false;
    bool suspending = false;
    bool resumed = false;
};

struct HardwareInfo {
    const char* platform = "Desktop";
    const char* model = "Desktop";
    const char* memoryProfile = "Desktop";
    std::size_t freeUserMemoryBytes = 0;
    std::size_t imageBudgetBytes = 0;
    bool freeMemoryMeasured = false;
};

bool initializeLifecycle();
LifecycleEvents pollLifecycleEvents();
void captureEarlyMemoryProfile();
std::size_t defaultImageMemoryBudget();
std::size_t clampImageMemoryBudget(std::size_t requestedBytes);
HardwareInfo hardwareInfo();
const char* name();

} // namespace Platform
