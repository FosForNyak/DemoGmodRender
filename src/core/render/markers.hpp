// =============================================================================
//  markers.hpp — позначки на шкалі демо і розділи (chapters) у відео.
//
//  Позначка — тік демо і назва. Позначки зберігаються окремо для кожного демо
//  (файл gmdr_markers.json у папці програми; демо впізнається за іменем і
//  розміром, тож переміщення файлу їх не губить). Під час рендеру позначки, що
//  потрапили у фрагмент, стають розділами MP4/MOV/MKV: плеєри показують їх на
//  шкалі, а YouTube — як таймкоди.
// =============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace gmdr::render {

struct Marker {
    int32_t     tick = 0;
    std::string title;
    bool operator==(const Marker&) const = default;
};

// Текстове представлення для налаштувань: рядок на позначку, "тік<TAB>назва".
std::vector<Marker> parse_markers(std::string_view text);
std::string         format_markers(const std::vector<Marker>& markers);
// Додати/замінити, зберігаючи порядок за часом.
void add_marker(std::vector<Marker>& markers, Marker m);

struct Chapter {
    double      start = 0;   // с від початку відео
    double      end = 0;
    std::string title;
};

// Розділи для фрагмента [start_tick, end_tick): кожна позначка всередині — початок
// розділу. Якщо перша позначка не на самому початку, перший розділ — "Початок".
std::vector<Chapter> chapters_for_range(const std::vector<Marker>& markers, int32_t start_tick, int32_t end_tick,
                                        double tick_interval);
// Таймкоди для опису відео на YouTube ("0:00 Початок" ...), у т.ч. для файлу поруч із відео.
std::string chapters_as_text(const std::vector<Chapter>& chapters);

// Позначки конкретного демо у спільному файлі.
std::vector<Marker> load_demo_markers(const std::filesystem::path& store, const std::string& demo_path_utf8);
bool save_demo_markers(const std::filesystem::path& store, const std::string& demo_path_utf8,
                       const std::vector<Marker>& markers, std::string* error = nullptr);

} // namespace gmdr::render
