// =============================================================================
//  versions.hpp — додаткові версії відео за один рендер.
//
//  Гра рендерить демо один раз, а з тих самих кадрів одночасно кодуються кілька
//  файлів: основний (з налаштувань) і вибрані тут версії — Discord до 10 МБ,
//  легка 480p, вертикальна 9:16 для Shorts/TikTok, ProRes для монтажу.
//  Файли лягають поруч з основним: відео_discord.mp4, відео_vertical.mp4 ...
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "encode_session.hpp"

namespace gmdr::render {

struct VersionPreset {
    std::string id;      // для налаштувань і --also: discord, 480p, vertical, master, thumb, gif, webp
    std::string label;   // для інтерфейсу
    std::string hint;    // пояснення
    bool        after_render = false;   // робиться з готового файлу після рендеру (обкладинка, анімації)
};
const std::vector<VersionPreset>& version_presets();

// Налаштування вибраних версій ("discord,vertical") для основного відео main.
// seconds — тривалість (для розміру файлу Discord); невідомі id пропускаються.
std::vector<ExtraOutput> make_extra_outputs(const std::string& ids, const EncodeSettings& main, double seconds);

// Після рендеру: обкладинка, GIF, WebP з готового основного файлу. Повертає створені файли;
// помилки — у журнал (основне відео вже готове, тож рендер через них не провалюється).
std::vector<std::string> make_post_versions(const std::string& ids, const std::string& main_path);

// Чи всі id зі списку відомі (для перевірки параметра --also); unknown — перший невідомий.
bool valid_version_ids(const std::string& ids, std::string* unknown = nullptr);

} // namespace gmdr::render
