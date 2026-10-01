#pragma once

#include <functional>
#include <string_view>

namespace gmdr {

enum class LogLevel { Debug, Info, Warn, Error };

using LogSink = std::function<void(LogLevel level, std::string_view module, std::string_view message)>;

// Process-wide sink; the default writes nothing. Messages must not contain personal data (player names,
// SteamIDs) at Info level and above.
void setLogSink(LogSink sink);
void log(LogLevel level, std::string_view module, std::string_view message);

inline void logInfo(std::string_view module, std::string_view message) { log(LogLevel::Info, module, message); }
inline void logWarn(std::string_view module, std::string_view message) { log(LogLevel::Warn, module, message); }
inline void logError(std::string_view module, std::string_view message) { log(LogLevel::Error, module, message); }

const char* logLevelName(LogLevel level);

} // namespace gmdr
