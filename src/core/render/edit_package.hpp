// =============================================================================
//  edit_package.hpp — пакет для монтажу: проєкт Final Cut Pro 7 XML (xmeml v5).
//
//  Поруч із відео — тека "назва_монтаж" з окремими WAV (гра, кожен гравець, мікрофон),
//  що починаються рівно з першого кадру відео, і файл проєкту. Premiere Pro і DaVinci
//  Resolve відкривають xmeml: відео на доріжці V1, кожен WAV — на своїй аудіодоріжці,
//  позначки фрагмента — маркерами на шкалі.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "markers.hpp"

namespace gmdr::render {

struct EditProject {
    std::string name;                 // назва послідовності
    std::string video_path;           // UTF-8, абсолютний
    int         width = 1920, height = 1080;
    int         fps_num = 60, fps_den = 1;
    int64_t     frames = 0;           // тривалість у кадрах
    bool        video_has_audio = true;
    struct Stem {
        std::string title;            // "Гра", нік гравця, "Мікрофон"
        std::string path;             // WAV, UTF-8, абсолютний
    };
    std::vector<Stem>    stems;
    std::vector<Chapter> markers;
};

// Текст проєкту xmeml (UTF-8).
std::string make_fcp7_xml(const EditProject& p);

// file://localhost/C%3a/... — як шляхи пише сам Premiere (UTF-8, відсотки для не-ASCII)
std::string fcp_path_url(const std::string& path_utf8);

// Безпечне ім'я файлу з назви доріжки (без <>:"/\|?*).
std::string safe_file_name(const std::string& title);

} // namespace gmdr::render
