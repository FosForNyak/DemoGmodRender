#include "core/log.h"

#include <mutex>

namespace gmdr {

namespace {
std::mutex& sinkMutex() {
    static std::mutex m;
    return m;
}
LogSink& sinkRef() {
    static LogSink s;
    return s;
}
} // namespace

void setLogSink(LogSink sink) {
    std::lock_guard lock(sinkMutex());
    sinkRef() = std::move(sink);
}

void log(LogLevel level, std::string_view module, std::string_view message) {
    std::lock_guard lock(sinkMutex());
    if (sinkRef())
        sinkRef()(level, module, message);
}

const char* logLevelName(LogLevel level) {
    switch (level) {
    case LogLevel::Debug:
        return "debug";
    case LogLevel::Info:
        return "info";
    case LogLevel::Warn:
        return "warn";
    case LogLevel::Error:
        return "error";
    }
    return "info";
}

} // namespace gmdr
