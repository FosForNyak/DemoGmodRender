// =============================================================================
//  resume.hpp — дописування рендеру, урваного збоєм самої програми чи ПК.
//
//  Поки йде рендер, поруч із налаштуваннями програми лежить запис стану (resume/<id>.json):
//  з якими налаштуваннями рендер, час першого кадру і WAV гри. Звичайне завершення (готово,
//  помилка, скасування) його прибирає — лишається він лише після збою. Тоді частковий файл
//  (фрагментований MP4/MOV чи MKV — вони відкриваються й обірваними) обрізається до
//  останнього ключового кадру, решта дорендерюється і склеюється без перекодування, а звук
//  міксується заново на всю довжину (зі збереженими WAV зірваного сеансу).
// =============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "settings.hpp"

namespace gmdr::render {

struct ResumeRecord {
    std::string    id;
    RenderSettings settings;          // налаштування рендеру (фрагмент, файл, кодек...)
    double         video_t0 = 0;      // час демо (с) першого кадру відео
    int64_t        frames = 0;        // скільки кадрів уже закодовано (для підказки)
    double         seconds = 0;       // тривалість відео, якщо дійде до кінця
    std::vector<std::pair<std::string, double>> wavs;   // WAV гри (UTF-8) і час демо першого семпла
    int64_t        updated = 0;       // коли записано (unix-час)
};

std::filesystem::path resume_dir();
bool save_resume(const ResumeRecord& r, std::string* error = nullptr);
std::optional<ResumeRecord> load_resume(const std::filesystem::path& file);
// Прибрати запис (і допоміжне в теці частин; частковий файл лишається — на своєму місці)
void forget_resume(const std::string& id);
// Урвані рендери, які можна дописати (частковий файл є), — найновіші першими
std::vector<ResumeRecord> pending_resumes();

// Тека частин поруч із відео і частковий файл у ній, коли дописування вже почалось
std::filesystem::path parts_dir_for(const std::string& output_path);
std::filesystem::path resume_head_path(const std::string& output_path);

} // namespace gmdr::render
