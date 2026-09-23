// =============================================================================
//  http_download.hpp — завантажити файл за HTTPS (модель розпізнавання мовлення).
//
//  Лише коли користувач сам натисне «Завантажити». Спершу пишеться <файл>.part,
//  після успіху — перейменування; перервали — .part видаляється.
// =============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace gmdr {

// progress(завантажено, усього байтів; 0 — невідомо). Windows — WinHTTP; інші ОС — помилка.
bool download_file(const std::string& url, const std::filesystem::path& dest,
                   const std::function<void(uint64_t, uint64_t)>& progress, const std::atomic<bool>* cancel,
                   std::string* error);

} // namespace gmdr
