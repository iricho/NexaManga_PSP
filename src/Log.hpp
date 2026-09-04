#pragma once

#include "BuildConfig.hpp"

#include <string>

namespace Log {

enum class Level {
    Info,
    Warning,
    Error
};

void write(Level level, const std::string& message);
void initializePersistentLog(const char* path);

inline void info(const std::string& message) { write(Level::Info, message); }
inline void warning(const std::string& message) { write(Level::Warning, message); }
inline void error(const std::string& message) { write(Level::Error, message); }

} // namespace Log

#if MANGAPSP_DEVELOPMENT
#  define MANGAPSP_LOG_INFO(expression) ::Log::info((expression))
#else
#  define MANGAPSP_LOG_INFO(expression) do { } while (false)
#endif

#if MANGAPSP_DEVELOPMENT || MANGAPSP_RUNTIME_DIAGNOSTICS
#  define MANGAPSP_LOG_RUNTIME_INFO(expression) ::Log::info((expression))
#else
#  define MANGAPSP_LOG_RUNTIME_INFO(expression) do { } while (false)
#endif
