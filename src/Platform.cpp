#include "Platform.hpp"

#include "MemoryBudget.hpp"

#ifdef __PSP__
#include <pspkernel.h>
#include <psppower.h>
#include <pspsysmem.h>
#include <pspthreadman.h>
#endif

namespace {

volatile int pendingEvents = 0;
constexpr int ExitEvent = 1 << 0;
constexpr int SuspendEvent = 1 << 1;
constexpr int ResumeEvent = 1 << 2;

#ifdef __PSP__
bool memoryProfileInitialized = false;
MemoryBudget::PspMemoryClass memoryClass = MemoryBudget::PspMemoryClass::Standard32MiB;
std::size_t initialFreeUserBytes = 0;
bool initialFreeUserMeasured = false;

void initializeMemoryProfile() {
    if (memoryProfileInitialized) return;
    const SceSize measured = sceKernelMaxFreeMemSize();
    initialFreeUserBytes = static_cast<std::size_t>(measured);
    // This fallback can be reached if a caller omitted the early-main capture.
    // A tiny post-newlib result cannot identify the hardware, so remain on the
    // conservative profile instead of using the kernel-only model API.
    initialFreeUserMeasured = initialFreeUserBytes >= 1024u * 1024u;
    if (!initialFreeUserMeasured) initialFreeUserBytes = 0;
    memoryClass = MemoryBudget::classifyPsp(initialFreeUserBytes);
    memoryProfileInitialized = true;
}
#endif

#ifdef __PSP__
int exitCallback(int, int, void*) {
    pendingEvents |= ExitEvent;
    return 0;
}

int powerCallback(int, int powerInfo, void*) {
    if ((powerInfo & PSP_POWER_CB_SUSPENDING) != 0) pendingEvents |= SuspendEvent;
    if ((powerInfo & PSP_POWER_CB_RESUME_COMPLETE) != 0) pendingEvents |= ResumeEvent;
    return 0;
}

int callbackThread(SceSize, void*) {
    const SceUID exitId = sceKernelCreateCallback("MangaPSP Exit", exitCallback, nullptr);
    if (exitId >= 0) sceKernelRegisterExitCallback(exitId);

    const SceUID powerId = sceKernelCreateCallback("MangaPSP Power", powerCallback, nullptr);
    if (powerId >= 0) scePowerRegisterCallback(-1, powerId);

    sceKernelSleepThreadCB();
    return 0;
}
#endif

} // namespace

namespace Platform {

void captureEarlyMemoryProfile() {
#ifdef __PSP__
    // This must be the first statement of SDL_main. Newlib lazily reserves its
    // heap from the largest user-memory block, so measuring later only sees the
    // small external-allocation threshold. Unlike pspSdkTotalFreeUserMemSize(),
    // sceKernelMaxFreeMemSize() is also the user-mode query used by _sbrk itself.
    initializeMemoryProfile();
#endif
}

bool initializeLifecycle() {
#ifdef __PSP__
    const SceUID threadId = sceKernelCreateThread(
        "MangaPSP Callbacks", callbackThread, 0x11, 0x1000, PSP_THREAD_ATTR_USER, nullptr);
    return threadId >= 0 && sceKernelStartThread(threadId, 0, nullptr) >= 0;
#else
    return true;
#endif
}

LifecycleEvents pollLifecycleEvents() {
    const int events = pendingEvents;
    pendingEvents &= ~events;

    LifecycleEvents result;
    result.exitRequested = (events & ExitEvent) != 0;
    result.suspending = (events & SuspendEvent) != 0;
    result.resumed = (events & ResumeEvent) != 0;
    return result;
}

std::size_t defaultImageMemoryBudget() {
#ifdef __PSP__
    initializeMemoryProfile();
    return MemoryBudget::imageBudget(memoryClass);
#else
    return 128u * 1024u * 1024u;
#endif
}

std::size_t clampImageMemoryBudget(std::size_t requestedBytes) {
#ifdef __PSP__
    initializeMemoryProfile();
    return MemoryBudget::clampRequested(requestedBytes, memoryClass);
#else
    return requestedBytes == 0 ? defaultImageMemoryBudget() : requestedBytes;
#endif
}

HardwareInfo hardwareInfo() {
    HardwareInfo info;
    info.platform = name();
#ifdef __PSP__
    initializeMemoryProfile();
    info.model = "unknown (user-mode safe)";
    info.memoryProfile = MemoryBudget::label(memoryClass);
    info.freeUserMemoryBytes = initialFreeUserBytes;
    info.imageBudgetBytes = MemoryBudget::imageBudget(memoryClass);
    info.freeMemoryMeasured = initialFreeUserMeasured;
#else
    info.imageBudgetBytes = defaultImageMemoryBudget();
#endif
    return info;
}

const char* name() {
#ifdef __PSP__
    return "PSP";
#else
    return "Desktop";
#endif
}

} // namespace Platform
