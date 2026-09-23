// =============================================================================
//  library.hpp — бібліотека демо: усі .dem з теки гри і ваших тек.
//
//  Читається лише заголовок демо (1072 байти: карта, сервер, хто записав,
//  тривалість), тож список із сотень файлів складається за мить. Файл, що не є
//  демо (чи обрізаний), лишається в списку з поясненням.
// =============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gmdr::demo {

struct LibraryEntry {
    std::string path;            // UTF-8
    std::string name;            // ім'я файлу
    std::string folder;          // тека (для показу)
    uint64_t    size = 0;
    int64_t     modified = 0;    // unix-час
    std::string map, server, recorded_by;
    double      seconds = 0;     // 0 — невідомо (гра не дописала заголовок)
    std::string error;           // не демо / пошкоджене
};

// Тека гри: garrysmod/ (там пише "record") і garrysmod/demos/ з підтеками; інші — з підтеками.
std::vector<LibraryEntry> scan_demo_library(const std::vector<std::filesystem::path>& dirs);

// Рядок пошуку: усі слова мають знайтися в імені, карті, сервері чи імені гравця (без регістру).
bool library_match(const LibraryEntry& e, const std::string& query);

} // namespace gmdr::demo
