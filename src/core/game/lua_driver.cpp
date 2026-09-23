#include "lua_driver.hpp"

#include "../util/file_util.hpp"
#include "../util/json.hpp"
#include "../util/log.hpp"
#include "../util/strings.hpp"

#include <ctime>
#include <format>
#include <fstream>

namespace gmdr::game {

// Lua-скрипт, що працює в меню GMod. Сам текст лежить у gmdr_driver.lua,
// а CMake під час збірки вбудовує його сюди як масив байтів з нулем у кінці
// (gmdr_driver_lua.inc).
const char* driver_lua_source() {
    static const unsigned char bytes[] = {
#include "gmdr_driver_lua.inc"
    };
    return reinterpret_cast<const char*>(bytes);
}

namespace {
constexpr const char* kIncludeMarker = "-- GMDR (GMod Demo Render)";
constexpr const char* kIncludeLine = "pcall( include, \"gmdr_driver.lua\" ) -- GMDR (GMod Demo Render)";

fs::path menu_lua(const GModInstall& g) { return g.garrysmod / "lua" / "menu" / "menu.lua"; }
fs::path driver_lua(const GModInstall& g) { return g.garrysmod / "lua" / "menu" / "gmdr_driver.lua"; }
fs::path data_dir(const GModInstall& g) { return g.garrysmod / "data" / "gmdr"; }
} // namespace

DriverState driver_state(const GModInstall& g) {
    auto menu = read_file_text(menu_lua(g));
    auto drv = read_file_text(driver_lua(g));
    if (!menu || !drv || menu->find(kIncludeMarker) == std::string::npos) return DriverState::NotInstalled;
    if (*drv != driver_lua_source()) return DriverState::Outdated;
    return DriverState::Installed;
}

bool install_driver(const GModInstall& g, std::string* error) {
    auto menu = read_file_text(menu_lua(g), error);
    if (!menu) {
        if (error) *error = "не знайдено " + path_to_utf8(menu_lua(g)) + " — це точно папка Garry's Mod?";
        return false;
    }
    if (!write_file_text(driver_lua(g), driver_lua_source(), error)) return false;
    if (menu->find(kIncludeMarker) == std::string::npos) {
        // Резервна копія оригінального menu.lua (один раз)
        const fs::path backup = menu_lua(g).parent_path() / "menu.lua.gmdr_backup";
        std::error_code ec;
        if (!fs::exists(backup, ec)) write_file_text(backup, *menu, nullptr);
        std::string text = *menu;
        if (!text.empty() && text.back() != '\n') text += "\n";
        text += "\n";
        text += kIncludeLine;
        text += "\n";
        if (!write_file_text(menu_lua(g), text, error)) return false;
    }
    log_info("Драйвер рендеру встановлено у {}", path_to_utf8(driver_lua(g)));
    return true;
}

bool uninstall_driver(const GModInstall& g, std::string* error) {
    if (auto menu = read_file_text(menu_lua(g))) {
        std::string out;
        for (const auto& line : split(*menu, '\n', false)) {
            if (line.find(kIncludeMarker) != std::string::npos) continue;
            out += line + "\n";
        }
        while (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') out.pop_back();
        if (!write_file_text(menu_lua(g), out, error)) return false;
    }
    remove_file_quiet(driver_lua(g));
    log_info("Драйвер рендеру видалено");
    return true;
}

std::string make_job_cfg(const DriverJob& job, bool mute_engine_voice, const std::string& extra,
                         const std::map<std::string, std::string>& originals) {
    auto q = [](const std::string& s) { return "\"" + replace_all(s, "\"", "'") + "\""; };
    std::string flags;
    for (const auto& f : job.movie_flags) flags += " " + f;
    std::string c;
    c += "// GMod Demo Render — тимчасовий конфіг завдання " + job.id + "\n";
    c += "// Створено автоматично, можна видалити.\n";
    c += "sv_cheats 1\n";
    if (job.mode == "watch") {
        // Перегляд: звичайне відтворення, налаштування гравця не чіпаємо
        c += "host_framerate 0\n";
        c += "alias gmdr_play " + q("playdemo " + job.demo) + "\n";
        if (job.seek_tick > 0) c += std::format("alias gmdr_seek \"demo_gototick {} 0 0\"\n", job.seek_tick);
        c += "alias gmdr_quit \"quit\"\n";
        c += "alias gmdr_restore \"\"\n";
        c += "echo \"[GMDR] конфіг перегляду завантажено\"\n";
        return c;
    }
    c += std::format("host_framerate {}\n", job.host_framerate);
    c += "snd_fixed_rate 1\n";
    c += "fps_max 0\n";
    c += "mat_vsync 0\n";
    // Обмеження FPS без фокуса (fps_max_nofocus) знімає драйвер у меню: він запам'ятовує значення
    // гравця і повертає його наприкінці. (engine_no_focus_sleep і con_drawnotify з новіших гілок
    // Source у GMod немає.)
    c += "snd_mute_losefocus 0\n";
    c += "net_graph 0\n";
    c += "cl_showfps 0\n";
    if (mute_engine_voice) c += "voice_scale 0\n";
    if (job.hide_hud) c += "cl_drawhud 0\n";
    if (job.hide_viewmodel) c += "r_drawviewmodel 0\n";
    if (!trim(extra).empty()) {
        c += "// Додаткові команди користувача\n";
        for (const auto& line : split(replace_all(extra, "\r", ""), '\n')) c += trim(line) + "\n";
    }
    c += "alias gmdr_play " + q("playdemo " + job.demo) + "\n";
    c += "alias gmdr_start " + q("startmovie " + job.movie + flags) + "\n";
    c += "alias gmdr_stop \"endmovie\"\n";
    if (job.seek_tick > 0) c += std::format("alias gmdr_seek \"demo_gototick {} 0 0\"\n", job.seek_tick);
    c += "alias gmdr_quit \"quit\"\n";
    // Відновлення налаштувань, які ми змінили (щоб вони не збереглися у config.cfg)
    std::string restore;
    for (const auto& [k, v] : originals) restore += std::format("{} {};", k, v);
    c += "alias gmdr_restore " + q(restore) + "\n";
    c += "echo \"[GMDR] конфіг завдання завантажено\"\n";
    return c;
}

bool write_job_files(const GModInstall& g, const DriverJob& job, const std::string& cfg_text, bool with_driver_job,
                     std::string* error) {
    json::Value j = json::Value::object();
    j.set("id", json::Value::string(job.id));
    j.set("created", json::Value::number(static_cast<double>(std::time(nullptr))));
    j.set("demo", json::Value::string(job.demo));
    j.set("movie", json::Value::string(job.movie));
    json::Value flags = json::Value::array();
    for (const auto& f : job.movie_flags) flags.push(json::Value::string(f));
    j.set("movie_flags", flags);
    j.set("host_framerate", json::Value::number(job.host_framerate));
    j.set("start_tick", json::Value::number(job.start_tick));
    j.set("end_tick", json::Value::number(job.end_tick));
    j.set("seek_tick", json::Value::number(job.seek_tick));
    j.set("quit", json::Value::boolean(job.quit_when_done));
    j.set("wait_next", json::Value::boolean(job.wait_next));
    j.set("menu_delay", json::Value::number(job.menu_delay));
    j.set("load_timeout", json::Value::number(job.load_timeout));
    j.set("mode", json::Value::string(job.mode));
    j.set("tick_interval", json::Value::number(job.tick_interval));
    std::error_code ec;
    fs::create_directories(data_dir(g), ec);
    remove_file_quiet(data_dir(g) / ("status_" + job.id + ".txt"));
    remove_file_quiet(data_dir(g) / ("cancel_" + job.id + ".txt"));
    if (!write_file_text(g.garrysmod / "cfg" / "gmdr" / ("job_" + job.id + ".cfg"), cfg_text, error)) return false;
    if (!with_driver_job) {
        remove_file_quiet(data_dir(g) / "job.txt");
        return true;
    }
    return write_file_atomic(data_dir(g) / "job.txt", j.dump(), error);
}

std::vector<DriverMark> read_marks(const GModInstall& g, const std::string& id) {
    std::vector<DriverMark> out;
    auto text = read_file_text(data_dir(g) / ("marks_" + id + ".txt"));
    if (!text) return out;
    for (const auto& line : split(replace_all(*text, "\r", ""), '\n')) {
        const auto parts = split(trim(line), ' ');
        if (parts.size() != 2) continue;
        const auto tick = parse_int(parts[1]);
        if (!tick || (parts[0] != "start" && parts[0] != "end" && parts[0] != "mark")) continue;
        out.push_back({parts[0], static_cast<int32_t>(*tick)});
    }
    return out;
}

void remove_job_files(const GModInstall& g, const std::string& id) {
    remove_file_quiet(data_dir(g) / "job.txt");
    remove_file_quiet(data_dir(g) / ("marks_" + id + ".txt"));
    remove_file_quiet(data_dir(g) / ("status_" + id + ".txt"));
    remove_file_quiet(data_dir(g) / ("cancel_" + id + ".txt"));
    remove_file_quiet(g.garrysmod / "cfg" / "gmdr" / ("job_" + id + ".cfg"));
}

void request_cancel(const GModInstall& g, const std::string& id) {
    write_file_text(data_dir(g) / ("cancel_" + id + ".txt"), "cancel", nullptr);
}

std::optional<DriverStatus> read_status(const GModInstall& g, const std::string& id) {
    auto text = read_file_text(data_dir(g) / ("status_" + id + ".txt"));
    if (!text || text->empty()) return std::nullopt;
    auto j = json::parse(*text);
    if (!j || !j->is_object()) return std::nullopt;
    DriverStatus s;
    s.state = (*j)["state"].as_string();
    s.message = (*j)["message"].as_string();
    s.tick = static_cast<int32_t>((*j)["tick"].as_int());
    s.total = static_cast<int32_t>((*j)["total"].as_int());
    s.start_tick = static_cast<int32_t>((*j)["start_tick"].as_int(-1));
    s.last_tick = static_cast<int32_t>((*j)["last_tick"].as_int(-1));
    s.frames = (*j)["frames"].as_int();
    s.playing = (*j)["playing"].as_bool();
    s.time = (*j)["time"].as_number();
    return s;
}

std::map<std::string, std::string> read_config_values(const GModInstall& g, const std::vector<std::string>& names) {
    // Типові значення GMod (якщо змінної немає в config.cfg)
    static const std::map<std::string, std::string> defaults = {
        {"host_framerate", "0"}, {"snd_fixed_rate", "0"}, {"fps_max", "300"}, {"mat_vsync", "0"},
        {"snd_mute_losefocus", "1"}, {"net_graph", "0"}, {"cl_showfps", "0"},
        {"voice_scale", "1"}, {"cl_drawhud", "1"}, {"r_drawviewmodel", "1"},
        {"sv_cheats", "0"}};
    std::map<std::string, std::string> out;
    for (const auto& n : names) {
        auto it = defaults.find(n);
        out[n] = it != defaults.end() ? it->second : "0";
    }
    if (auto text = read_file_text(g.garrysmod / "cfg" / "config.cfg")) {
        for (const auto& line_raw : split(*text, '\n')) {
            const std::string line = trim(line_raw);
            const size_t sp = line.find(' ');
            if (sp == std::string::npos) continue;
            const std::string key = line.substr(0, sp);
            if (!out.count(key)) continue;
            std::string val = trim(line.substr(sp + 1));
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
            if (val.find_first_of(";\"") == std::string::npos) out[key] = val;
        }
    }
    return out;
}

bool backup_config(const GModInstall& g, const fs::path& backup_path) {
    std::error_code ec;
    const fs::path cfg = g.garrysmod / "cfg" / "config.cfg";
    if (!fs::exists(cfg, ec)) return false;
    return copy_file_overwrite(cfg, backup_path);
}

bool restore_config(const GModInstall& g, const fs::path& backup_path) {
    std::error_code ec;
    if (!fs::exists(backup_path, ec)) return false;
    std::string err;
    if (!copy_file_overwrite(backup_path, g.garrysmod / "cfg" / "config.cfg", &err)) {
        log_warn("Не вдалося відновити config.cfg: {}", err);
        return false;
    }
    fs::remove(backup_path, ec);
    return true;
}

std::vector<std::string> console_log_tail(const GModInstall& g, size_t lines) {
    auto text = read_file_text(g.garrysmod / "console.log");
    if (!text) return {};
    auto all = split(*text, '\n');
    if (all.size() > lines) all.erase(all.begin(), all.end() - static_cast<ptrdiff_t>(lines));
    for (auto& l : all) l = trim(l);
    return all;
}

} // namespace gmdr::game
