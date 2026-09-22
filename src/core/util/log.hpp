// =============================================================================
//  log.hpp — простий потокобезпечний журнал (лог) для всієї програми.
//
//  Будь-який модуль викликає gmdr::log_info("...{}...", значення) і т.д.
//  GUI та CLI підписуються на повідомлення через add_log_sink() і самі
//  вирішують, куди їх виводити (у вікно, у консоль, у файл).
// =============================================================================
#pragma once

#include <format>
#include <functional>
#include <string>
#include <utility>

namespace gmdr {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

// "Приймач" повідомлень журналу. Викликається з будь-якого потоку!
using LogSink = std::function<void(LogLevel, const std::string&)>;

// Додати приймача. Повертає ідентифікатор, за яким його можна видалити.
int  add_log_sink(LogSink sink);
void remove_log_sink(int id);

// Повідомлення з рівнем нижче за мінімальний ігноруються.
void     set_min_log_level(LogLevel level);
LogLevel min_log_level();

void log_message(LogLevel level, const std::string& text);

const char* log_level_name(LogLevel level);

template <class... Args>
void log_debug(std::format_string<Args...> fmt, Args&&... args) {
    if (min_log_level() <= LogLevel::Debug)
        log_message(LogLevel::Debug, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void log_info(std::format_string<Args...> fmt, Args&&... args) {
    log_message(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void log_warn(std::format_string<Args...> fmt, Args&&... args) {
    log_message(LogLevel::Warn, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void log_error(std::format_string<Args...> fmt, Args&&... args) {
    log_message(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace gmdr
