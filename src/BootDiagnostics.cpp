#include "BootDiagnostics.hpp"

#include "BuildConfig.hpp"

#include <SDL.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#if defined(__PSP__) && MANGAPSP_BOOT_TRACE_ACTIVE
#include <pspdebug.h>
#include <pspkernel.h>
#endif

namespace {

volatile int gLastStage = 0;
char gLogPath[512] = "mangapsp-boot.log";
char gLastCheckpoint[16] = "none";

#if defined(__PSP__) && MANGAPSP_BOOT_TRACE_ACTIVE
bool gLogReady = false;

void visibleText(const char* first, const char* second) {
    pspDebugScreenInit();
    pspDebugScreenSetXY(0, 0);
    pspDebugScreenPrintf("%s\n%s\n", first ? first : "NexaManga PSP", second ? second : "");
}

#if MANGAPSP_DEVELOPMENT
void holdOnRenderer(int stage, SDL_Renderer* renderer) {
    const Uint8 red = static_cast<Uint8>(40 + (stage * 13) % 180);
    const Uint8 green = static_cast<Uint8>(35 + (stage * 29) % 150);
    SDL_SetRenderDrawColor(renderer, 12, 25, 65, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, red, green, 235, 255);
    SDL_Rect outer {24, 48, 432, 176};
    SDL_RenderFillRect(renderer, &outer);
    SDL_SetRenderDrawColor(renderer, 245, 245, 245, 255);
    SDL_Rect marker {36, 190, stage * 24, 18};
    SDL_RenderFillRect(renderer, &marker);
    SDL_RenderPresent(renderer);
    while (true) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {}
        SDL_Delay(100);
    }
}
#endif

#endif

#if defined(__PSP__) && MANGAPSP_BOOT_TRACE_ACTIVE
void appendLog(const char* format, ...) {
    if (!gLogReady) return;

    // A crash can occur before a process-wide FILE* is closed. On PSP Memory
    // Stick storage that can leave the directory entry at zero bytes even when
    // fflush() was called. Commit each diagnostic record independently so the
    // last completed checkpoint survives a power-off.
    FILE* log = std::fopen(gLogPath, "a");
    if (!log) return;

    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(log, format, arguments);
    va_end(arguments);
    std::fflush(log);
    std::fclose(log);
}

void writeLine(const char* format, const char* key, const char* value) {
    appendLog(format, key ? key : "", value ? value : "");
}
#endif

} // namespace

namespace BootDiagnostics {

void initialize(const char* executablePath) {
#if defined(__PSP__) && MANGAPSP_BOOT_TRACE_ACTIVE
    if (executablePath && *executablePath) {
        const char* slash = std::strrchr(executablePath, '/');
        if (slash) {
            const std::size_t directoryLength = static_cast<std::size_t>(slash - executablePath);
            if (directoryLength + sizeof("/mangapsp-boot.log") < sizeof(gLogPath)) {
                std::memcpy(gLogPath, executablePath, directoryLength);
                gLogPath[directoryLength] = '\0';
                std::strcat(gLogPath, "/mangapsp-boot.log");
            }
        }
    }
    FILE* log = std::fopen(gLogPath, "w");
    if (!log && std::strcmp(gLogPath, "mangapsp-boot.log") != 0) {
        std::strcpy(gLogPath, "mangapsp-boot.log");
        log = std::fopen(gLogPath, "w");
    }
    if (log) {
        std::fprintf(log, "NexaManga PSP boot diagnostics\nlog=%s\n", gLogPath);
        std::fprintf(log,
            "exception_handler=not installed (PSPSDK implementation requires kernel module)\n");
        std::fflush(log);
        std::fclose(log);
        gLogReady = true;
    }
    // pspDebugInstallErrorHandler is not installed here: the current PSPSDK
    // implementation resolves sceKernelRegisterDefaultExceptionHandler from
    // libpspkernel, and its official sample is a kernel module. MangaPSP is an
    // SDL2main-owned user-mode module, so forcing it would alter startup mode.
#else
    (void)executablePath;
#endif
}

void stage(int number, const char* label) {
    gLastStage = number;
#if defined(__PSP__) && MANGAPSP_BOOT_TRACE_ACTIVE
    appendLog("STAGE %02d %s\n", number, label ? label : "");
#if MANGAPSP_DEVELOPMENT
    if (number == 1) visibleText("NexaManga PSP boot diagnostics", "01 main entered");
    else if (number <= 3) pspDebugScreenPrintf("%02d %s\n", number, label ? label : "");
#endif
#else
    (void)label;
#endif
}

void checkpoint(int number, const char* label) {
    std::snprintf(gLastCheckpoint, sizeof(gLastCheckpoint), "03.%d", number - 30);
#if defined(__PSP__) && MANGAPSP_DEVELOPMENT && MANGAPSP_BOOT_DIAGNOSTICS
    appendLog("CHECKPOINT %s %s\n", gLastCheckpoint, label ? label : "");
    // These checkpoints bracket SDL_Init. Write first, then make the same
    // checkpoint visible without reinitializing the debug framebuffer.
    pspDebugScreenPrintf("%s %s\n", gLastCheckpoint, label ? label : "");
#else
    (void)label;
#endif
}

void checkpointLogOnly(const char* id, const char* label) {
    std::snprintf(gLastCheckpoint, sizeof(gLastCheckpoint), "%s", id ? id : "unknown");
#if defined(__PSP__) && MANGAPSP_DEVELOPMENT && MANGAPSP_BOOT_DIAGNOSTICS
    // Once SDL owns video/renderer state, writing through pspDebugScreen can
    // touch the same framebuffer. Later startup checkpoints are log-only.
    appendLog("CHECKPOINT %s %s\n", gLastCheckpoint, label ? label : "");
#else
    (void)label;
#endif
}

void note(const char* key, const char* value) {
#if defined(__PSP__) && MANGAPSP_BOOT_TRACE_ACTIVE
    writeLine("%s=%s\n", key, value);
#else
    (void)key;
    (void)value;
#endif
}

void noteUnsigned(const char* key, unsigned long value) {
#if defined(__PSP__) && MANGAPSP_BOOT_TRACE_ACTIVE
    appendLog("%s=%lu\n", key ? key : "", value);
#else
    (void)key;
    (void)value;
#endif
}

void holdIfRequested(int number, SDL_Renderer* renderer) {
#if defined(__PSP__) && MANGAPSP_DEVELOPMENT && MANGAPSP_BOOT_DIAGNOSTICS
    if (MANGAPSP_BOOT_HOLD_STAGE != number) return;
    noteUnsigned("hold_stage", static_cast<unsigned long>(number));
    if (renderer) holdOnRenderer(number, renderer);
    char message[80];
    std::snprintf(message, sizeof(message), "Holding at boot stage %02d", number);
    visibleText("NexaManga PSP diagnostic hold", message);
    while (true) sceKernelDelayThread(1000000);
#else
    (void)number;
    (void)renderer;
#endif
}

void holdIfCheckpointRequested(int number) {
#if defined(__PSP__) && MANGAPSP_DEVELOPMENT && MANGAPSP_BOOT_DIAGNOSTICS
    if (MANGAPSP_BOOT_HOLD_CHECKPOINT != number) return;
    appendLog("HOLD checkpoint=03.%d\n", number - 30);
    char message[80];
    std::snprintf(message, sizeof(message), "Holding at checkpoint 03.%d", number - 30);
    visibleText("NexaManga PSP diagnostic hold", message);
    while (true) sceKernelDelayThread(1000000);
#else
    (void)number;
#endif
}

void fatal(const char* message) {
#if defined(__PSP__) && MANGAPSP_BOOT_TRACE_ACTIVE
    appendLog("FATAL stage=%02d checkpoint=%s error=%s\n",
        gLastStage, gLastCheckpoint, message ? message : "unknown");
    char stageLine[80];
    std::snprintf(stageLine, sizeof(stageLine), "Stage %02d / %s: %s", gLastStage,
                  gLastCheckpoint, message ? message : "unknown error");
    visibleText("NexaManga PSP startup failure", stageLine);
    pspDebugScreenPrintf("\nLog: %s\n", gLogPath);
    while (true) sceKernelDelayThread(1000000);
#else
    (void)message;
#endif
}

int lastStage() {
    return gLastStage;
}

const char* logPath() {
    return gLogPath;
}

} // namespace BootDiagnostics
