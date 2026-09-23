// =============================================================================
//  lua_driver.hpp — невеликий Lua-скрипт у меню GMod, що керує рендером.
//
//  Як це працює:
//   1) Ми кладемо garrysmod/lua/menu/gmdr_driver.lua і додаємо в кінець
//      garrysmod/lua/menu/menu.lua рядок  pcall(include, "gmdr_driver.lua")
//      (скрипт нічого не робить, якщо немає файлу завдання).
//   2) Перед запуском гри пишемо завдання в garrysmod/data/gmdr/job.txt
//      і конфіг garrysmod/cfg/gmdr/job_<id>.cfg (host_framerate, аліаси ...).
//   3) Скрипт у меню: запускає демо, чекає, поки воно реально почне грати
//      (зникне екран завантаження), на потрібному тіку виконує startmovie,
//      в кінці — endmovie і закриває гру. Стан пише в data/gmdr/status_<id>.txt.
//
//  Видалити драйвер: кнопка в програмі або "Перевірити цілісність файлів" у Steam.
// =============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "gmod_install.hpp"

namespace gmdr::game {

const char* driver_lua_source();
constexpr const char* kDriverVersion = "1.6";

enum class DriverState { NotInstalled, Installed, Outdated };
DriverState driver_state(const GModInstall& g);
bool install_driver(const GModInstall& g, std::string* error);
bool uninstall_driver(const GModInstall& g, std::string* error);

struct DriverJob {
    std::string id;
    std::string demo;          // шлях для playdemo (відносно garrysmod/)
    std::string movie;         // префікс кадрів для startmovie (відносно garrysmod/)
    std::vector<std::string> movie_flags = {"raw"};
    double      host_framerate = 60.0;
    int32_t     start_tick = 0;
    int32_t     end_tick = -1;
    int32_t     seek_tick = -1;       // > 0: спершу швидко перемотати демо сюди (demo_gototick)
    bool        quit_when_done = true;
    bool        wait_next = false;    // черга: після запису не закривати гру, чекати наступний job.txt
    double      menu_delay = 3.0;     // с: пауза після старту меню
    double      load_timeout = 600.0; // с
    bool        hide_hud = false;
    bool        hide_viewmodel = false;
    // "render" — запис; "watch" — перегляд у грі в реальному часі з клавішами-позначками
    // (F9 — початок, F11 — кінець фрагмента, F6 — позначка; див. read_marks)
    std::string mode = "render";
    double      tick_interval = 1.0 / 66.0;   // для показу часу в підказках
};

// Вміст конфігу завдання (+ аліаси gmdr_*). originals — початкові значення
// змінних з config.cfg, щоб наприкінці їх повернути (аліас gmdr_restore).
std::string make_job_cfg(const DriverJob& job, bool mute_engine_voice, const std::string& extra_commands,
                         const std::map<std::string, std::string>& originals);

bool write_job_files(const GModInstall& g, const DriverJob& job, const std::string& cfg_text, bool with_driver_job,
                     std::string* error);
void remove_job_files(const GModInstall& g, const std::string& id);
void request_cancel(const GModInstall& g, const std::string& id);

struct DriverStatus {
    std::string state;        // menu / loading / arming / recording / stopping / done / quit / error
    std::string message;
    int32_t     tick = 0;
    int32_t     total = 0;
    int32_t     start_tick = -1;   // тік першого записаного кадру
    int32_t     last_tick = -1;
    int64_t     frames = 0;
    bool        playing = false;
    double      time = 0;          // SysTime() гри
};
std::optional<DriverStatus> read_status(const GModInstall& g, const std::string& id);

// Позначки, зроблені клавішами під час перегляду (data/gmdr/marks_<id>.txt): "start 123" ...
struct DriverMark {
    std::string kind;   // start / end / mark
    int32_t     tick = 0;
};
std::vector<DriverMark> read_marks(const GModInstall& g, const std::string& id);

// Значення змінних у config.cfg (щоб відновити після рендеру).
std::map<std::string, std::string> read_config_values(const GModInstall& g, const std::vector<std::string>& names);
// Резервна копія config.cfg на час рендеру і відновлення.
bool backup_config(const GModInstall& g, const std::filesystem::path& backup_path);
bool restore_config(const GModInstall& g, const std::filesystem::path& backup_path);

// Останні рядки консолі гри (console.log при запуску з -condebug).
std::vector<std::string> console_log_tail(const GModInstall& g, size_t lines);

} // namespace gmdr::game
