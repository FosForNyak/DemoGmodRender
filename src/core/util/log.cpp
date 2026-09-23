#include "log.hpp"
#include "i18n.hpp"

#include <atomic>
#include <map>
#include <mutex>

namespace gmdr {

namespace {
std::mutex                g_mutex;
std::map<int, LogSink>    g_sinks;
int                       g_next_id = 1;
std::atomic<int>          g_min_level{static_cast<int>(LogLevel::Info)};
} // namespace

int add_log_sink(LogSink sink) {
    std::lock_guard lock(g_mutex);
    const int id = g_next_id++;
    g_sinks.emplace(id, std::move(sink));
    return id;
}

void remove_log_sink(int id) {
    std::lock_guard lock(g_mutex);
    g_sinks.erase(id);
}

void set_min_log_level(LogLevel level) { g_min_level = static_cast<int>(level); }
LogLevel min_log_level() { return static_cast<LogLevel>(g_min_level.load()); }

const char* log_level_name(LogLevel level) {
    switch (level) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO";
    case LogLevel::Warn:  return tr("УВАГА");
    case LogLevel::Error: return tr("ПОМИЛКА");
    }
    return "?";
}

namespace {
thread_local std::string t_prefix;   // напр. "[частина 2] " — для паралельного рендеру
} // namespace

void set_thread_log_prefix(std::string prefix) { t_prefix = std::move(prefix); }

void log_message(LogLevel level, const std::string& text_in) {
    if (static_cast<int>(level) < g_min_level.load()) return;
    const std::string& text = t_prefix.empty() ? text_in : t_prefix + text_in;
    // Копіюємо список приймачів під замком, а викликаємо без замка —
    // так приймач може сам щось логувати і не буде взаємоблокування.
    std::map<int, LogSink> sinks;
    {
        std::lock_guard lock(g_mutex);
        sinks = g_sinks;
    }
    for (auto& [id, sink] : sinks) {
        if (sink) sink(level, text);
    }
}

} // namespace gmdr
