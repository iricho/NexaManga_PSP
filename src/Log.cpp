#include "Log.hpp"

#include <cstdio>
#include <cstring>

namespace {

#if defined(__PSP__) && MANGAPSP_RUNTIME_DIAGNOSTICS
char persistentLogPath[512] {};
bool persistentLogReady = false;
#endif

} // namespace

namespace Log {

void initializePersistentLog(const char* path) {
#if defined(__PSP__) && MANGAPSP_RUNTIME_DIAGNOSTICS
    if (!path || !*path) return;
    std::snprintf(persistentLogPath, sizeof(persistentLogPath), "%s", path);
    persistentLogReady = persistentLogPath[0] != '\0';
#else
    (void)path;
#endif
}

void write(Level level, const std::string& message) {
#if !defined(NDEBUG) || MANGAPSP_ENABLE_LOGGING
    const char* label = "INFO";
    if (level == Level::Warning) label = "WARN";
    if (level == Level::Error) label = "ERROR";
    std::fprintf(stderr, "[NexaManga PSP][%s] %s\n", label, message.c_str());
    std::fflush(stderr);
#if defined(__PSP__) && MANGAPSP_RUNTIME_DIAGNOSTICS
    if (persistentLogReady) {
        FILE* log = std::fopen(persistentLogPath, "a");
        if (log) {
            std::fprintf(log, "[NexaManga PSP][%s] %s\n", label, message.c_str());
            std::fflush(log);
            std::fclose(log);
        }
    }
#endif
#else
    (void)level;
    (void)message;
#endif
}

} // namespace Log
