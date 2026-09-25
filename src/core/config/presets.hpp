// =============================================================================
//  presets.hpp — готові набори налаштувань «в один клік» (YouTube, Discord,
//  монтаж, архів) і зміна формату файлу.
//
//  Пресет — це лише зміни налаштувань; перевіряє результат той самий рушій
//  (constraints.hpp), що й будь-які інші налаштування. Вибір, що залежить від
//  комп'ютера (HEVC на відеокарті, якщо вона вміє), бере з EnvironmentCapabilities.
//  Однаково для вікна, CLI (--profile) і черги.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "capabilities.hpp"

namespace gmdr::config {

struct PresetInfo {
    std::string id;            // youtube-1080p60, discord-10mb ...
    std::string label;         // український ключ перекладу
    std::string description;   // український ключ перекладу
    std::string gpu_family;    // пресет бере GPU-кодек цього сімейства, якщо проба пройшла (hevc); порожньо — ні
};
const std::vector<PresetInfo>& presets();
const PresetInfo*              find_preset(const std::string& id);

// Застосувати пресет (новий набір налаштувань; решта — як була). Невідомий id — без змін.
render::RenderSettings apply_preset(const render::RenderSettings& s, const std::string& id, const EnvironmentCapabilities& env);

// Змінити формат файлу: розширення вихідного файлу (і шаблон кадрів для послідовностей).
// Кодеки не підміняються мовчки — несумісність покаже перевірка з виправленням.
// Виняток — послідовність кадрів: у неї єдиний можливий кодек (png, tiff ...).
void set_container(render::RenderSettings& s, const std::string& ext);

} // namespace gmdr::config
