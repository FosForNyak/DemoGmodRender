// =============================================================================
//  settings_catalog.hpp — опис кожного налаштування: ідентифікатор, ключ JSON,
//  група, тип, підпис і режим (стандартний / розширений).
//
//  SettingId генерується зі списку полів RenderSettings (GMDR_SETTINGS_FIELDS),
//  тож нове поле автоматично має ідентифікатор і читається/пишеться за ним
//  (get_setting / set_setting) — так працюють виправлення з перевірки
//  налаштувань, інтерфейс і діагностика для розробника.
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../render/settings.hpp"
#include "../util/json.hpp"

namespace gmdr::config {

enum class SettingId {
#define X(name) name,
    GMDR_SETTINGS_FIELDS(X)
#undef X
    Count
};

enum class SettingGroup {
    Source,        // демо, фрагмент
    Game,          // рендерер, копія гри, як гра рендерить
    Output,        // файл, контейнер
    Video,         // розмір, FPS, кодек, якість
    Motion,        // розмиття руху, швидкість
    Audio,         // звук, голоси, мікрофон, обробка
    Subtitles,     // субтитри, розпізнавання
    Translation,   // переклад
    Dubbing,       // озвучення
    Performance,   // паралельність, потоки, диск
    Outputs,       // додаткові версії
    Application,   // вигляд, мова, поведінка вікна (на рендер не впливає)
};
const char* group_id(SettingGroup g);   // "source", "game" ...

enum class SettingType { Bool, Int, Double, String };

struct SettingInfo {
    SettingId    id;
    std::string  key;          // ключ JSON і ідентифікатор для інтерфейсу
    SettingGroup group;
    SettingType  type;
    std::string  label;        // український ключ перекладу (порожньо — службове поле)
    bool         advanced = false;   // лише в розширеному режимі
    bool         secret = false;     // API-ключ: зберігається зашифрованим, у звіт не потрапляє
    bool         render = true;      // впливає на рендер (false — уподобання вікна)
};

const std::vector<SettingInfo>& settings_catalog();
const SettingInfo&              setting_info(SettingId id);
std::optional<SettingId>        find_setting(std::string_view key);
const std::string&              setting_key(SettingId id);

// Значення налаштування за ідентифікатором (як у JSON)
json::Value get_setting(const render::RenderSettings& s, SettingId id);
// Записати значення (тип перетворюється як у from_json); false — невідомий тип значення
bool        set_setting(render::RenderSettings& s, SettingId id, const json::Value& v);

} // namespace gmdr::config
