// =============================================================================
//  report.hpp — звіт про проблему: один ZIP, який користувач може надіслати сам.
//
//  Усередині: журнал програми, налаштування, черга, відомості про систему (ОС,
//  процесор, пам'ять, відеокарти, FFmpeg, GMod і драйвер), кінець консолі GMod і
//  останній дамп збою. У текстових файлах шлях до профілю користувача замінено на
//  %USERPROFILE% (у ньому ім'я облікового запису). Нічого нікуди не надсилається.
// =============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace gmdr::render {

struct ReportInput {
    std::string settings_path;   // gmdr_settings.json
    std::string extra_text;      // додатково в система.txt (напр. результати перевірки GPU-кодеків)
};

// Відомості про систему (текст для система.txt).
std::string system_summary();

// Прибрати з тексту шлях до профілю користувача (C:\Users\Ім'я -> %USERPROFILE%).
std::string anonymize_paths(std::string text, const std::string& profile_dir);

// Створити архів. Повертає список того, що в нього потрапило (для показу користувачу).
bool make_problem_report(const std::filesystem::path& out_zip, const ReportInput& in,
                         std::vector<std::string>* contents, std::string* error);

// Типова назва: gmdr_звіт_2026-09-23_14-05.zip
std::string default_report_name();

} // namespace gmdr::render
